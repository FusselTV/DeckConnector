#include "db_satellite.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_log.h"
#include "db_net.h"
#include "db_sat.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fcntl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_port.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "ws.h"

#define LINE_CAP       (256 * 1024) /* an rgb strip part is 80 KB of base64; Companion's own limit is 2 MB */
#define RX_CHUNK       8192
#define RX_WAIT_MS     200
#define SEND_WAIT_MS   500
#define CONNECT_MS     3000
#define RETRY_MS       2000
#define REFUSED_MS     500 /* refused or reset: Companion is starting, its port opens soon */
#define REFUSED_MAX    40  /* then RETRY_MS */
#define FAIL_GAP_MS    250
#define PING_MS        2000
#define SILENT_MS      6000 /* Companion answers every PING */
#define ADD_AGAIN_MS   3000 /* a "Device exists elsewhere" goes to the old session's socket, not to us */
#define REFUSED_MAX_MS 60000
#define LAN_PROBE_MS   5000
#define BOOT_LAN_US    (30LL * 1000000)
#define QUALITY        90
#define CACHE_SLOTS    2048
#define CACHE_BUDGET   (4 * 1024 * 1024)
#define PNG_PREFIX     "data:image/png;base64,"
#define WRONG_PIN_MS   600

/* "host" or "host:port" from the satellite setting: the host into `host`, and the port. */
static unsigned tcp_host(const char *addr, char *host, size_t cap)
{
    snprintf(host, cap, "%s", addr);
    char *colon = strchr(host, ':');
    if (!colon) {
        return DB_SAT_PORT;
    }
    *colon = '\0';
    return (unsigned)atoi(colon + 1);
}

static const char *TAG = "satellite";

typedef struct {
    char id[24];
    uint8_t *jpeg; /* NULL: free slot */
    size_t len;
} cached_t;

/* One deck as Companion knows it, by DEVICEID: its serial. */
typedef struct {
    volatile bool added;
    volatile bool dirty;  /* came, went or changed: Companion gets the new layout */
    db_display_t display; /* format and orientation the deck takes */
    bool swipe;
    int brightness;
    bool was_writer;
    int64_t add_sent_ms;
    uint32_t refused_ms; /* the wait before it is offered again after a refusal, 0: none */
    db_sat_lock_t pin;   /* under pin_mux: read on the esp_timer task */
    int64_t wrong_until_ms;
    db_sat_held_t held; /* the deck's event task only */
    char serial[32];
    char model[32];
    const char *surface;
    db_sat_control_t ctl[DB_SAT_MAX_CONTROLS];
    size_t nctl;
} sat_dev_t;

static struct {
    void (*changed)(void);
    SemaphoreHandle_t lock; /* the controls, between the deck's event task and the client */
    SemaphoreHandle_t tx;
    volatile int sock;
    volatile uint32_t peer;
    struct {
        char addr[sizeof(((db_config_t *)0)->satellite)];
        uint32_t ip;
    } resolved; /* under resolved_mux */
    ws_t ws;    /* the WebSocket port (16623), when the address is a ws:// or wss:// URL */
    bool ws_mode;
    uint64_t frame_left;
    char pin[sizeof(((db_config_t *)0)->satellite_pin)];
    volatile bool broken;
    bool png;
    bool rotary_amount;
    bool caching;
    bool over_wlan;
    bool to_lan;
    db_health_t health;
    TaskHandle_t task;
    char addr[sizeof(((db_config_t *)0)->satellite)];
    char host[sizeof(((db_config_t *)0)->satellite)];
    unsigned port;
    sat_dev_t devs[DB_MAX_DECKS]; /* by slot */
    uint8_t *line;
    size_t fill;
    uint8_t *rgb; /* B, G, R for the encoder, as big as the largest control of any deck */
    uint8_t *work;
    size_t work_cap;
    uint32_t seq;
    cached_t *cache;
} s = {.sock = -1};

static portMUX_TYPE pin_mux = portMUX_INITIALIZER_UNLOCKED;

/* Companion sends the lock again with the next ADD-DEVICE. */
static void unlock(sat_dev_t *v)
{
    portENTER_CRITICAL(&pin_mux);
    v->pin = (db_sat_lock_t){0};
    portEXIT_CRITICAL(&pin_mux);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Under `tx`. Over the WebSocket one frame carries one line, as Companion sends them. A failed or short send
 * ends the session: a lost key-up would leave the key held in Companion, and the deck's input task must not
 * wait. */
static void raw_send(const char *buf, size_t n)
{
    if (s.sock < 0 || s.broken) {
        return;
    }
    bool ok = s.ws_mode ? ws_send(&s.ws, WS_OP_TEXT, buf, n) == 0 : send(s.sock, buf, n, 0) == (int)n;
    if (!ok) {
        ESP_LOGW(TAG, "send failed, ending the session");
        s.broken = true;
        shutdown(s.sock, SHUT_RDWR);
    }
}

static void send_line(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    xSemaphoreTake(s.tx, portMAX_DELAY);
    if (n > 0 && (size_t)n < sizeof(buf)) {
        raw_send(buf, (size_t)n);
    }
    xSemaphoreGive(s.tx);
}

/* Companion's bytes: straight from TCP, or out of the WebSocket frames that carry them. Like recv(). */
static int sat_recv(uint8_t *buf, size_t cap)
{
    if (!s.ws_mode) {
        return recv(s.sock, buf, cap, 0);
    }
    while (s.frame_left == 0) {
        uint8_t op;
        bool fin;
        uint64_t len;
        int err = ws_read_header(&s.ws, &op, &fin, &len);
        if (!err && op == WS_OP_CLOSE) {
            return 0;
        }
        if (!err && op == WS_OP_PING && len <= 125) {
            uint8_t ping[125];
            err = ws_read(&s.ws, ping, (size_t)len);
            err = err ? err : ws_send(&s.ws, WS_OP_PONG, ping, (size_t)len);
        } else if (!err && op != WS_OP_TEXT && op != WS_OP_BINARY && op != WS_OP_CONT) {
            err = ws_skip(&s.ws, len);
        } else if (!err) {
            s.frame_left = len;
        }
        if (err) {
            errno = -err;
            return -1;
        }
    }
    size_t n = s.frame_left < cap ? (size_t)s.frame_left : cap;
    int err = ws_read(&s.ws, buf, n);
    if (err) {
        errno = -err;
        return -1;
    }
    s.frame_left -= n;
    return (int)n;
}

static void cache_clear(void)
{
    for (int i = 0; i < CACHE_SLOTS; i++) {
        heap_caps_free(s.cache[i].jpeg);
        s.cache[i].jpeg = NULL;
    }
}

static cached_t *cache_find(const char *id)
{
    for (int i = 0; i < CACHE_SLOTS; i++) {
        if (s.cache[i].jpeg && strcmp(s.cache[i].id, id) == 0) {
            return &s.cache[i];
        }
    }
    return NULL;
}

/* Companion keeps the books of what the bridge holds and tells it what to drop. Each entry is reported at
 * no less than its share of the budget, so the slots never run out before the budget does. */
static bool cache_put(const char *id, const uint8_t *jpeg, size_t len)
{
    cached_t *c = cache_find(id);
    for (int i = 0; i < CACHE_SLOTS && !c; i++) {
        c = s.cache[i].jpeg ? NULL : &s.cache[i];
    }
    uint8_t *copy = c && strlen(id) < sizeof(c->id) ? heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : NULL;
    if (!copy) {
        return false;
    }
    memcpy(copy, jpeg, len);
    heap_caps_free(c->jpeg);
    *c = (cached_t){.jpeg = copy, .len = len};
    strcpy(c->id, id);
    size_t cost = len > CACHE_BUDGET / CACHE_SLOTS ? len : CACHE_BUDGET / CACHE_SLOTS;
    send_line("BITMAP-COST BITMAP_ID=\"%s\" BYTES=%u\n", id, (unsigned)cost);
    return true;
}

static void cache_evict(char *ids)
{
    for (char *id = strtok(ids, ","); id; id = strtok(NULL, ",")) {
        cached_t *c = cache_find(id);
        if (c) {
            heap_caps_free(c->jpeg);
            c->jpeg = NULL;
        }
    }
}

/* The deck of a message's DEVICEID, else -1. */
static int deck_of(const db_sat_msg_t *m)
{
    const char *id = db_sat_get(m, "DEVICEID");
    for (int d = 0; id && d < DB_MAX_DECKS; d++) {
        if (s.devs[d].nctl && strcmp(s.devs[d].serial, id) == 0) {
            return d;
        }
    }
    return -1;
}

static bool find_control(int deck, const char *id, db_sat_control_t *out)
{
    const sat_dev_t *v = &s.devs[deck];
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const db_sat_control_t *c = id ? db_sat_find(v->ctl, v->nctl, id) : NULL;
    if (c) {
        *out = *c;
    }
    xSemaphoreGive(s.lock);
    return c != NULL;
}

/* A bitmap as Companion sends it (a PNG data URL, or raw R, G, B) to an image in the deck's format. */
static esp_err_t to_jpeg(const db_display_t *display, char *bitmap, db_rect_t r, uint8_t **jpeg, size_t *len)
{
    bool png = strncmp(bitmap, PNG_PREFIX, strlen(PNG_PREFIX)) == 0;
    char *b64 = png ? bitmap + strlen(PNG_PREFIX) : bitmap;
    size_t n = 0;
    size_t px = (size_t)r.w * r.h;
    /* in place: mbedtls writes 3 bytes only after it read the 4 characters they come from */
    if (mbedtls_base64_decode((unsigned char *)b64, strlen(b64), &n, (const unsigned char *)b64,
                              strlen(b64)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (png) {
        if (db_png_decode((uint8_t *)b64, n, r.w, r.h, s.rgb, s.work, s.work_cap) != DB_OK) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (n == px * 3) {
        for (size_t i = 0; i < px; i++) {
            s.rgb[i * 3] = (uint8_t)b64[i * 3 + 2];
            s.rgb[i * 3 + 1] = (uint8_t)b64[i * 3 + 1];
            s.rgb[i * 3 + 2] = (uint8_t)b64[i * 3];
        }
    } else {
        return ESP_ERR_INVALID_SIZE;
    }
    return jpeg_port_encode_for(display, s.rgb, r.w, r.h, QUALITY, jpeg, len);
}

static void draw(int deck, const db_sat_control_t *c, uint8_t *jpeg, size_t len)
{
    db_deck_submit(deck, DB_SOURCE_SAT, 0, c->rect, ++s.seq, jpeg, len, (uint64_t)esp_timer_get_time());
}

/* false: the picture cannot be drawn as Companion thinks, and only a new session puts that right. */
static bool key_state(int deck, const db_sat_msg_t *m)
{
    db_sat_control_t c;
    if (deck < 0 || !s.rgb || !s.work || !find_control(deck, db_sat_get(m, "CONTROLID"), &c) || !c.rect.w) {
        return true;
    }
    char *bitmap = (char *)db_sat_get(m, "BITMAP");
    const char *id = db_sat_get(m, "BITMAP_ID");
    uint8_t *jpeg = NULL;
    size_t len = 0;
    if (bitmap && bitmap[0]) {
        esp_err_t err = to_jpeg(&s.devs[deck].display, bitmap, c.rect, &jpeg, &len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "bitmap for %u/%u not drawn: %s", c.row, c.col, esp_err_to_name(err));
            return true;
        }
        if (id && s.caching && !cache_put(id, jpeg, len)) {
            ESP_LOGW(TAG, "bitmap %s not cached", id);
            heap_caps_free(jpeg);
            return false;
        }
    } else if (id) {
        cached_t *hit = cache_find(id);
        jpeg = hit ? heap_caps_malloc(hit->len, MALLOC_CAP_SPIRAM) : NULL;
        if (!jpeg) {
            ESP_LOGW(TAG, "bitmap %s %s", id, hit ? "no memory" : "not in the cache");
            return false;
        }
        memcpy(jpeg, hit->jpeg, hit->len);
        len = hit->len;
    } else {
        return true;
    }
    draw(deck, &c, jpeg, len);
    return true;
}

static void clear_keys(int deck)
{
    const sat_dev_t *v = &s.devs[deck];
    for (size_t i = 0; s.rgb && i < v->nctl; i++) {
        const db_sat_control_t *c = &v->ctl[i];
        uint8_t *jpeg = NULL;
        size_t len = 0;
        if (c->rect.w) {
            memset(s.rgb, 0, (size_t)c->rect.w * c->rect.h * 3);
            if (jpeg_port_encode_for(&v->display, s.rgb, c->rect.w, c->rect.h, QUALITY, &jpeg, &len) ==
                ESP_OK) {
                draw(deck, c, jpeg, len);
            }
        }
    }
}

static void add_device(int deck)
{
    sat_dev_t *v = &s.devs[deck];
    char *json = malloc(2048);
    char *b64 = malloc(2800);
    char *line = malloc(3200);
    size_t n = 0;
    bool sent = false;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    size_t len = json ? db_sat_manifest(v->ctl, v->nctl, json, 2048) : 0;
    xSemaphoreGive(s.lock);
    if (len && b64 && line &&
        mbedtls_base64_encode((unsigned char *)b64, 2800, &n, (unsigned char *)json, len) == 0) {
        size_t k = db_sat_add_device(line, 3200, v->surface, v->serial, v->model, b64, s.png, v->swipe);
        xSemaphoreTake(s.tx, portMAX_DELAY);
        if (k) {
            raw_send(line, k);
            sent = true;
        }
        xSemaphoreGive(s.tx);
    }
    if (!sent) {
        ESP_LOGW(TAG, "deck not offered to Companion (no memory), trying again");
    }
    v->add_sent_ms = now_ms(); /* without an answer session() sends it again */
    free(json);
    free(b64);
    free(line);
}

static bool size_buffers(void)
{
    size_t px = 0;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        for (size_t i = 0; i < s.devs[d].nctl; i++) {
            size_t a = (size_t)s.devs[d].ctl[i].rect.w * s.devs[d].ctl[i].rect.h;
            px = a > px ? a : px;
        }
    }
    heap_caps_free(s.rgb);
    heap_caps_free(s.work);
    s.rgb = px ? jpeg_port_alloc_rgb(px, 1) : NULL;
    s.work_cap = px * 4 + 1024; /* rows of RGBA plus a filter byte each */
    s.work = px ? heap_caps_malloc(s.work_cap, MALLOC_CAP_SPIRAM) : NULL;
    return !px || (s.rgb && s.work);
}

/* The deck came, went or changed: Companion gets the new layout. */
static void apply_device(int deck)
{
    sat_dev_t *v = &s.devs[deck];
    db_device_info_t *dev = malloc(sizeof(*dev));
    if (!dev) {
        return; /* still dirty: the next round tries again, a missing deck would take it from Companion */
    }
    v->dirty = false;
    v->add_sent_ms = 0;
    v->refused_ms = 0;
    if (v->added) {
        send_line("REMOVE-DEVICE DEVICEID=\"%s\"\n", v->serial);
        v->added = false;
        unlock(v);
        s.changed();
    }
    bool present = db_deck_info(deck, dev);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    v->nctl = present ? db_sat_controls(dev, v->ctl, DB_SAT_MAX_CONTROLS) : 0;
    xSemaphoreGive(s.lock);
    if (present) {
        snprintf(v->serial, sizeof(v->serial), "%s", dev->serial);
        snprintf(v->model, sizeof(v->model), "%s", dev->model);
        v->surface = dev->surface;
        v->display = dev->displays[0];
    }
    v->swipe = present && dev->touch_count > 0;
    free(dev);
    if (!size_buffers()) {
        ESP_LOGW(TAG, "no memory for the deck's pictures, trying again");
        v->dirty = true;
    } else if (v->nctl) {
        add_device(deck);
    }
}

static void all_dirty(void)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        s.devs[d].dirty = true;
    }
}

/* false ends the session. */
static bool handle(char *line)
{
    db_sat_msg_t m;
    db_sat_parse(line, &m);
    const char *v;
    int deck = deck_of(&m);
    sat_dev_t *d = deck >= 0 ? &s.devs[deck] : NULL;
    if (strcmp(m.cmd, "KEY-STATE") == 0) {
        return key_state(deck, &m);
    } else if (strcmp(m.cmd, "BEGIN") == 0) {
        const char *api = db_sat_get(&m, "ApiVersion");
        v = db_sat_get(&m, "CompanionVersion");
        ESP_LOGI(TAG, "Companion %s, API %s", v ? v : "?", api ? api : "?");
    } else if (strcmp(m.cmd, "CAPS") == 0) {
        s.png = (v = db_sat_get(&m, "BITMAP_FORMATS")) && strstr(v, "png");
        s.rotary_amount = (v = db_sat_get(&m, "ROTARY_AMOUNT")) && strcmp(v, "1") == 0;
        if ((v = db_sat_get(&m, "BITMAP_CACHE")) && strcmp(v, "1") == 0) {
            send_line("BITMAP-CACHE BYTES=%u\n", (unsigned)CACHE_BUDGET);
        }
        all_dirty(); /* the session's loop offers every deck */
    } else if (strcmp(m.cmd, "BITMAP-CACHE") == 0) {
        s.caching = db_sat_get(&m, "OK") != NULL;
    } else if (strcmp(m.cmd, "BITMAP-EVICT") == 0) {
        char *ids = (char *)db_sat_get(&m, "BITMAP_IDS");
        if (db_sat_get(&m, "ALL")) {
            cache_clear();
        } else if (ids) {
            cache_evict(ids);
        }
    } else if (strcmp(m.cmd, "ADD-DEVICE") == 0 && d) {
        v = db_sat_get(&m, "MESSAGE");
        /* "already added": our resend crossed a slow OK */
        bool added = db_sat_get(&m, "OK") != NULL || (v && strstr(v, "already"));
        if (added && !d->added) {
            ESP_LOGI(TAG, "Companion took the %s %s", d->model, d->serial);
            db_log_mark(DB_MARK_SATELLITE);
            d->added = true;
            s.changed();
        }
        if (added) {
            d->add_sent_ms = 0;
            d->refused_ms = 0;
        } else {
            /* e.g. "Device exists elsewhere" while it has not noticed the last session ended: this deck
             * waits longer each time, the others stay */
            d->refused_ms = d->refused_ms
                                ? (d->refused_ms * 2 > REFUSED_MAX_MS ? REFUSED_MAX_MS : d->refused_ms * 2)
                                : ADD_AGAIN_MS;
            d->add_sent_ms = now_ms();
            ESP_LOGW(TAG, "Companion refused the %s: %s, again in %lu s", d->serial, v ? v : "?",
                     (unsigned long)(d->refused_ms / 1000));
        }
    } else if (strcmp(m.cmd, "BRIGHTNESS") == 0 && (v = db_sat_get(&m, "VALUE")) && d) {
        d->brightness = atoi(v);
        d->was_writer = false; /* applied below, now or once the deck is ours */
    } else if ((strcmp(m.cmd, "LOCKED-STATE") == 0 || strcmp(m.cmd, "PINCODE-KEY") == 0) && d) {
        int64_t now = now_ms();
        portENTER_CRITICAL(&pin_mux);
        db_sat_lock_t was = d->pin;
        if (db_sat_lock_update(&d->pin, &m)) {
            d->wrong_until_ms = now + WRONG_PIN_MS;
        }
        bool news = memcmp(&was, &d->pin, sizeof(was)) != 0;
        portEXIT_CRITICAL(&pin_mux);
        if (news) {
            s.changed();
        }
    } else if (strcmp(m.cmd, "KEYS-CLEAR") == 0 && d) {
        clear_keys(deck);
    } else if (strcmp(m.cmd, "ERROR") == 0) {
        ESP_LOGW(TAG, "Companion: %s", (v = db_sat_get(&m, "MESSAGE")) ? v : line);
    }
    return true;
}

static volatile uint32_t moves; /* saves of the address or pin, see db_satellite_moved */

static void session(uint32_t moved_at)
{
    s.fill = 0;
    s.caching = s.png = s.rotary_amount = false;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        s.devs[d].added = s.devs[d].was_writer = s.devs[d].dirty = false;
        s.devs[d].add_sent_ms = 0;
        s.devs[d].refused_ms = 0;
        s.devs[d].brightness = -1;
    }
    int64_t last_rx = now_ms();
    int64_t last_ping = last_rx;
    int64_t last_probe = last_rx;
    for (;;) {
        if (moves != moved_at || strcmp(s.addr, db_config()->satellite) != 0 ||
            strcmp(s.pin, db_config()->satellite_pin) != 0) {
            ESP_LOGI(TAG, "address changed");
            return;
        }
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            sat_dev_t *v = &s.devs[d];
            if (v->dirty) {
                apply_device(d);
            }
            bool writer = db_deck_writer(d) == DB_SOURCE_SAT;
            if (writer && !v->was_writer && v->brightness >= 0) {
                db_deck_set_brightness(d, (uint8_t)(v->brightness > 100 ? 100 : v->brightness));
            }
            v->was_writer = writer;
        }
        if (s.fill == LINE_CAP) {
            ESP_LOGW(TAG, "line longer than %d KB", LINE_CAP / 1024);
            return;
        }
        size_t want = LINE_CAP - s.fill < RX_CHUNK ? LINE_CAP - s.fill : RX_CHUNK;
        int n = sat_recv(s.line + s.fill, want);
        int64_t now = now_ms();
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            db_log_event(TAG, "source", "Companion lost: %s", n ? strerror(errno) : "closed");
            return;
        }
        if (n > 0) {
            last_rx = now;
            size_t start = 0;
            for (size_t i = s.fill; i < s.fill + (size_t)n; i++) {
                if (s.line[i] == '\n') {
                    s.line[i] = '\0';
                    if (!handle((char *)s.line + start)) {
                        return;
                    }
                    start = i + 1;
                }
            }
            s.fill += (size_t)n - start;
            memmove(s.line, s.line + start, s.fill);
        } else if (now - last_rx > SILENT_MS) {
            db_log_event(TAG, "source", "Companion silent for %d s", SILENT_MS / 1000);
            return;
        }
        if (now - last_ping >= PING_MS) {
            last_ping = now;
            send_line("PING\n");
        }
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            const sat_dev_t *v = &s.devs[d];
            if (!v->added && v->add_sent_ms &&
                now - v->add_sent_ms > (v->refused_ms ? v->refused_ms : ADD_AGAIN_MS)) {
                add_device(d);
            }
        }
        char lan[6];
        if (s.over_wlan && now - last_probe >= LAN_PROBE_MS && db_net_ifname(false, lan)) {
            last_probe = now;
            int probe = ws_tcp_open(s.host, s.port, lan, 1000); /* only a LAN that reaches Companion wins */
            if (probe >= 0) {
                close(probe);
                ESP_LOGI(TAG, "LAN is up, moving from WLAN");
                s.to_lan = true;
                return;
            }
        }
    }
}

/* The connection of the last boot, for end_old_session(). Addresses and ports in network order. */
typedef struct {
    uint32_t ip;
    uint32_t local_ip;
    uint16_t port;
    uint16_t local;
} old_t;

static void remember(int sock)
{
    struct sockaddr_in a = {0};
    struct sockaddr_in b = {0};
    socklen_t la = sizeof(a);
    socklen_t lb = sizeof(b);
    getpeername(sock, (struct sockaddr *)&a, &la);
    getsockname(sock, (struct sockaddr *)&b, &lb);
    old_t o = {a.sin_addr.s_addr, b.sin_addr.s_addr, a.sin_port, b.sin_port};
    nvs_handle_t h;
    if (nvs_open("satellite", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "old", &o, sizeof(o));
        nvs_commit(h);
        nvs_close(h);
    }
}

/*
 * After a power cut Companion keeps writing into the session of the last boot, so it does not time out, and
 * the deck stays taken ("Device exists elsewhere") until a retransmission reaches us, seconds later. The
 * reset db_dock sends to the Elgato App ends it at once: a SYN from the old port, Companion's stack
 * answers with an ACK, ours with a reset. True once done, or when there is nothing to end.
 */
static bool end_old_session(void)
{
    old_t o = {0};
    size_t len = sizeof(o);
    nvs_handle_t h;
    if (nvs_open("satellite", NVS_READONLY, &h) != ESP_OK) {
        return true;
    }
    esp_err_t err = nvs_get_blob(h, "old", &o, &len);
    nvs_close(h);
    if (err != ESP_OK || !o.ip) {
        return true;
    }
    /* not before: lwIP would send the SYN from the wrong address, or into a switch port that does not forward
     * yet */
    if (!db_net_path_ready(o.local_ip, o.ip)) {
        return esp_timer_get_time() > BOOT_LAN_US;
    }
    int sock = db_net_syn_from(o.local_ip, o.local, o.ip, o.port);
    if (sock >= 0) {
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_LOGI(TAG, "ended the session of the last boot");
        close(sock);
    }
    return true;
}

static portMUX_TYPE resolved_mux = portMUX_INITIALIZER_UNLOCKED;

/* Kept while the session is down or DNS fails: db_dock refuses Companion's dock connections by it. */
static void resolve(void)
{
    struct addrinfo hints = {.ai_family = AF_INET}, *res = NULL;
    if (getaddrinfo(s.host, NULL, &hints, &res) != 0 || !res) {
        return;
    }
    portENTER_CRITICAL(&resolved_mux);
    strcpy(s.resolved.addr, s.addr);
    s.resolved.ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    portEXIT_CRITICAL(&resolved_mux);
    freeaddrinfo(res);
}

/* LAN first, WLAN after a failed try, and back to LAN as soon as it reaches Companion. */
/* The deck says "not reachable" from the first failure on: it hears of the change. */
static void failed(const char *why)
{
    bool was = s.health.error[0] != '\0';
    db_health_fail(&s.health, why);
    if (!was) {
        s.changed();
    }
}

static void client_task(void *arg)
{
    bool via_wlan = false;
    bool old_ended = false;
    int last_err = 0;
    int refused = 0;
    for (;;) {
        uint32_t moved_at =
            moves; /* before the address is read: a save in between ends the session at once */
        snprintf(s.addr, sizeof(s.addr), "%s", db_config()->satellite);
        static char tried[sizeof(s.addr)];
        if (strcmp(tried, s.addr) != 0) { /* a new address has not failed yet */
            strcpy(tried, s.addr);
            if (s.health.error[0]) {
                s.health.error[0] = '\0';
                s.changed();
            }
        }
        if (!s.addr[0]) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RETRY_MS));
            continue;
        }
        db_net_status_t n;
        db_net_status(&n);
        if (!via_wlan && n.lan_link && !n.lan_up && esp_timer_get_time() < BOOT_LAN_US) {
            vTaskDelay(pdMS_TO_TICKS(250)); /* the cable is in and DHCP is on its way: no detour over WLAN */
            continue;
        }
        if (!old_ended && !(old_ended = end_old_session())) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        snprintf(s.pin, sizeof(s.pin), "%s", db_config()->satellite_pin);
        s.ws_mode = strncmp(s.addr, "ws://", 5) == 0 || strncmp(s.addr, "wss://", 6) == 0;
        if (s.ws_mode) {
            ws_url_host(s.addr, s.host, sizeof(s.host), &s.port);
        } else {
            s.port = tcp_host(s.addr, s.host, sizeof(s.host));
        }
        resolve();
        char ifname[6];
        bool wlan = via_wlan;
        bool bound = db_net_pick(&wlan, ifname);
        int sock = -1;
        if (s.ws_mode) {
            uint8_t pin[32];
            bool pinned = db_config_pin(s.pin, pin);
            sock =
                ws_connect(&s.ws, s.addr, NULL, NULL, bound ? ifname : NULL, CONNECT_MS, pinned ? pin : NULL);
            sock = sock ? sock : s.ws.sock;
            s.frame_left = 0;
        } else {
            sock = ws_tcp_open(s.host, s.port, bound ? ifname : NULL, CONNECT_MS);
        }
        if (sock < 0) {
            failed(strerror(-sock));
            if (sock != last_err) {
                ESP_LOGW(TAG, "%s: no connection (%s)", s.addr, strerror(-sock));
                last_err = sock;
            }
            /* right after a start the switch port may not pass traffic yet: LAN again, soon */
            bool early = !wlan && n.lan_link && esp_timer_get_time() < BOOT_LAN_US;
            bool soon = (sock == -ECONNREFUSED || sock == -ECONNRESET) && refused++ < REFUSED_MAX;
            via_wlan = early || soon ? false : !wlan;
            int64_t failed_at = now_ms();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(early ? 250 : soon ? REFUSED_MS : RETRY_MS));
            /* also when woken: a failed wss try can be 0.7 s of certificate check, back to back they starve
             * IDLE0 into the task watchdog */
            int64_t left = FAIL_GAP_MS - (now_ms() - failed_at);
            if (left > 0) {
                vTaskDelay(pdMS_TO_TICKS(left));
            }
            continue;
        }
        last_err = 0;
        refused = 0;
        struct timeval tv = {.tv_usec = RX_WAIT_MS * 1000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        struct timeval send_tv = {.tv_usec = SEND_WAIT_MS * 1000};
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
        s.broken = false;
        s.over_wlan = bound && wlan;
        db_health_ok(&s.health, s.over_wlan);
        remember(sock);
        db_log_event(TAG, "source", "Companion connected at %s over %s", s.addr,
                     bound ? (wlan ? "WLAN" : "LAN") : "the default route");
        struct sockaddr_in peer = {0};
        socklen_t plen = sizeof(peer);
        getpeername(sock, (struct sockaddr *)&peer, &plen);
        s.peer = peer.sin_addr.s_addr;
        s.sock = sock;
        s.changed(); /* Companion's dock connection from this peer ends, or it refuses the deck as taken */
        int64_t began = now_ms();
        session(moved_at);
        s.peer = 0;
        xSemaphoreTake(s.tx, portMAX_DELAY);
        s.sock = -1;
        if (s.ws_mode) {
            ws_close(&s.ws);
        } else {
            close(sock);
        }
        xSemaphoreGive(s.tx);
        bool had = false;
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            had |= s.devs[d].added;
            s.devs[d].added = false;
            unlock(&s.devs[d]);
            s.devs[d].nctl = 0; /* the next session's CAPS offers them anew */
            db_deck_forget(d, DB_SOURCE_SAT);
        }
        if (had) {
            s.changed();
        }
        failed("connection lost");
        cache_clear();
        heap_caps_free(s.rgb);
        heap_caps_free(s.work);
        s.rgb = s.work = NULL;
        via_wlan = false;
        /* soon after a lost session, Companion may be restarting; not after one it ended at once, e.g. by
         * refusing the deck */
        bool moved = s.to_lan || strcmp(s.addr, db_config()->satellite) != 0;
        vTaskDelay(pdMS_TO_TICKS(moved ? 0 : now_ms() - began < 1000 ? RETRY_MS : 250));
        s.to_lan = false;
    }
}

static void press(const sat_dev_t *v, const db_sat_control_t *c, bool down)
{
    send_line("KEY-PRESS DEVICEID=\"%s\" CONTROLID=\"%u/%u\" PRESSED=%d\n", v->serial, c->row, c->col, down);
}

static void rotate(const sat_dev_t *v, const db_sat_control_t *c, int ticks)
{
    if (s.rotary_amount) {
        send_line("KEY-ROTATE DEVICEID=\"%s\" CONTROLID=\"%u/%u\" DIRECTION=%d\n", v->serial, c->row, c->col,
                  ticks);
        return;
    }
    for (int i = 0; i < abs(ticks); i++) {
        send_line("KEY-ROTATE DEVICEID=\"%s\" CONTROLID=\"%u/%u\" DIRECTION=%d\n", v->serial, c->row, c->col,
                  ticks > 0);
    }
}

static const db_sat_control_t *control_of(const sat_dev_t *v, db_sat_kind_t kind, uint8_t index)
{
    for (size_t i = 0; i < v->nctl; i++) {
        if (v->ctl[i].kind == kind && v->ctl[i].index == index) {
            return &v->ctl[i];
        }
    }
    return NULL;
}

/* As the deck plugin does it: a tap presses the strip part under the finger, a sideways swipe turns
 * the page, an upward or downward one turns the part. */
static void on_input(int deck, const db_input_t *ev)
{
    sat_dev_t *v = &s.devs[deck];
    /* a press made while the deck was locked or showed the menu is not Companion's, nor is its release */
    if (!db_sat_pass(&v->held, ev,
                     v->added && db_deck_writer(deck) == DB_SOURCE_SAT && db_deck_input_real_for(s.peer))) {
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const db_sat_control_t *c = NULL;
    switch (ev->kind) {
    case DB_INPUT_KEY_DOWN:
    case DB_INPUT_KEY_UP:
        if ((c = control_of(v, DB_SAT_KEY, ev->index))) {
            press(v, c, ev->kind == DB_INPUT_KEY_DOWN);
        }
        break;
    case DB_INPUT_ENCODER_DOWN:
    case DB_INPUT_ENCODER_UP:
        if ((c = control_of(v, DB_SAT_ENCODER, ev->index))) {
            press(v, c, ev->kind == DB_INPUT_ENCODER_DOWN);
        }
        break;
    case DB_INPUT_ENCODER_TURN:
        if ((c = control_of(v, DB_SAT_ENCODER, ev->index))) {
            rotate(v, c, ev->value);
        }
        break;
    case DB_INPUT_TOUCH_TAP:
    case DB_INPUT_TOUCH_LONG:
        if ((c = db_sat_lcd_at(v->ctl, v->nctl, ev->x))) {
            press(v, c, true);
            press(v, c, false);
        }
        break;
    case DB_INPUT_TOUCH_SWIPE: {
        int dx = abs(ev->x2 - ev->x);
        int dy = abs(ev->y2 - ev->y);
        c = db_sat_lcd_at(v->ctl, v->nctl, ev->x);
        if (c && 100 * dy >= 119 * dx &&
            c == db_sat_lcd_at(v->ctl, v->nctl, ev->x2)) { /* steeper than 50 deg */
            rotate(v, c, ev->y > ev->y2 ? 1 : -1);
        } else if (1000 * dy <= 414 * dx) { /* flatter than 22.5 degrees */
            send_line("CHANGE-PAGE DEVICEID=\"%s\" DIRECTION=%d\n", v->serial, ev->x > ev->x2);
        }
        break;
    }
    }
    xSemaphoreGive(s.lock);
}

static void on_device(int deck, const db_device_info_t *info)
{
    s.devs[deck].dirty = true;
}

esp_err_t db_satellite_start(void (*changed)(void))
{
    s.changed = changed;
    s.lock = xSemaphoreCreateMutex();
    s.tx = xSemaphoreCreateMutex();
    s.line = heap_caps_malloc(LINE_CAP, MALLOC_CAP_SPIRAM);
    s.cache = heap_caps_calloc(CACHE_SLOTS, sizeof(cached_t), MALLOC_CAP_SPIRAM);
    if (!s.lock || !s.tx || ws_init(&s.ws) || !s.line || !s.cache) {
        return ESP_ERR_NO_MEM;
    }
    db_deck_listener_t dl = {.input = on_input, .device_changed = on_device, .external = true};
    db_deck_add_listener(&dl);
    /* tinfl, which inflates the PNGs, keeps 11 KB of state on the stack */
    return xTaskCreatePinnedToCore(client_task, "satellite", 16384, NULL, 6, &s.task, 0) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool db_satellite_connected(int deck)
{
    bool added = false;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        added |= s.devs[d].added && (deck < 0 || d == deck);
    }
    return added && strcmp(s.addr, db_config()->satellite) == 0; /* not the old address after a change */
}

db_health_t db_satellite_health(void)
{
    return s.health;
}

void db_satellite_moved(void)
{
    moves++;
    db_satellite_retry();
}

void db_satellite_retry(void)
{
    if (s.task) {
        xTaskNotifyGive(s.task);
    }
}

bool db_satellite_locked(int deck, db_sat_pin_t *out)
{
    const sat_dev_t *v = &s.devs[deck];
    int64_t now = now_ms();
    portENTER_CRITICAL(&pin_mux);
    db_sat_lock_t pin = v->pin;
    out->wrong = now < v->wrong_until_ms;
    portEXIT_CRITICAL(&pin_mux);
    unsigned typed = (unsigned)pin.count + pin.pending; /* a dot at once, Companion's count follows */
    out->count = (uint8_t)(typed > 255 ? 255 : typed);
    out->rotation = pin.rotation;
    return pin.locked && db_satellite_connected(deck);
}

void db_satellite_pincode(int deck, int digit)
{
    sat_dev_t *v = &s.devs[deck];
    db_sat_pin_t pin;
    if (!db_satellite_locked(deck, &pin) || !db_deck_input_real_for(s.peer)) {
        return;
    }
    portENTER_CRITICAL(&pin_mux);
    v->pin.pending = (uint8_t)(v->pin.pending < 255 ? v->pin.pending + 1 : 255);
    portEXIT_CRITICAL(&pin_mux);
    send_line("PINCODE-KEY DEVICEID=\"%s\" KEY=%d\n", v->serial, digit);
}

uint32_t db_satellite_peer(void)
{
    return s.peer;
}

uint32_t db_satellite_resolved(void)
{
    portENTER_CRITICAL(&resolved_mux);
    uint32_t ip = strcmp(s.resolved.addr, db_config()->satellite) == 0 ? s.resolved.ip : 0;
    portEXIT_CRITICAL(&resolved_mux);
    return ip;
}

static const char *connect_error(int err)
{
    static char why[64]; /* web handlers run one at a time */
    if (err == -ENXIO) {
        return "name not found";
    }
    if (err == -ETIMEDOUT) {
        return "no connection within 3 s";
    }
    if (err == -ECONNREFUSED || err == -ECONNRESET) {
        return "connection refused";
    }
    snprintf(why, sizeof(why), "no connection: %s", strerror(-err));
    return why;
}

const char *db_satellite_probe(const char *addr, const char *pin_hex, char *version, size_t cap)
{
    char line[160];
    size_t n = 0;
    if (strncmp(addr, "ws://", 5) == 0 || strncmp(addr, "wss://", 6) == 0) {
        ws_t *w = calloc(1, sizeof(*w));
        if (!w || ws_init(w)) {
            free(w);
            return "no memory";
        }
        uint8_t pin[32];
        bool pinned = db_config_pin(pin_hex, pin);
        uint8_t op;
        bool fin;
        uint64_t len = 0;
        int err = ws_connect(w, addr, NULL, NULL, NULL, CONNECT_MS, pinned ? pin : NULL);
        const char *why = err ? (w->fail ? w->fail : connect_error(err)) : NULL;
        if (!why) {
            err = ws_read_header(w, &op, &fin, &len);
            n = len < sizeof(line) - 1 ? (size_t)len : sizeof(line) - 1;
            why = err || ws_read(w, line, n) ? "no answer within 3 s" : NULL;
        }
        ws_close(w);
        ws_deinit(w);
        free(w);
        if (why) {
            return why;
        }
    } else {
        char host[64];
        unsigned port = tcp_host(addr, host, sizeof(host));
        int sock = ws_tcp_open(host, port, NULL, CONNECT_MS);
        if (sock < 0) {
            return connect_error(sock);
        }
        int64_t end =
            esp_timer_get_time() + CONNECT_MS * 1000LL; /* a byte at a time must not hold the web lock */
        for (int r; n < sizeof(line) - 1 && !memchr(line, '\n', n) && esp_timer_get_time() < end;
             n += (size_t)r) {
            if ((r = recv(sock, line + n, sizeof(line) - 1 - n, 0)) <= 0) {
                break;
            }
        }
        close(sock);
        if (!n) {
            return "no answer within 3 s";
        }
    }
    line[n] = '\0';
    line[strcspn(line, "\r\n")] = '\0';
    db_sat_msg_t m;
    db_sat_parse(line, &m);
    const char *v = strcmp(m.cmd, "BEGIN") == 0 ? db_sat_get(&m, "CompanionVersion") : NULL;
    if (!v) {
        return "it answers, but it is not Companion's Satellite API";
    }
    snprintf(version, cap, "%s", v);
    return NULL;
}

const char *db_satellite_address(void)
{
    return db_config()->satellite;
}
