#include "db_link.h"
#include "db_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_board.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_proto.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "sdkconfig.h"
#include "ws.h"

#define LINKS            DB_SURFACE_LINKS
#define TX_INLINE        32
#define TX_QUEUE         128
#define PROBE_TIMEOUT    1000
#define BOOT_LAN_WAIT_US (30LL * 1000000)
#define TICK_MS          500
#define DISCOVER_EVERY   10 /* ticks; 12 times as long while a host draws */
#define RETRY_MS         2000
#define RETRY_MAX_MS     30000
#define CONNECT_TIMEOUT  3000
#define PING_AFTER_MS    2000
#define SILENT_LIMIT_MS  6000
#define ALL_LINKS        (-1)
#define CACHE_BYTES      (8 * 1024 * 1024)
#define CACHE_SLOTS      1024
#define DECK_FRAMES      4 /* FRAME and STREAM in flight per deck */
#define FLIGHT           (DB_MAX_DECKS * DECK_FRAMES)
#define SHOTS_MAX        64 /* (target, slot) pairs in one FRAME or FALLBACK */
#define HELLO_MAX        8192
#define PAIRED_MAX       256

_Static_assert(LINKS <= DB_SOURCE_LINKS, "one deck source per link");
_Static_assert(FLIGHT <= 256, "the flight index is the low byte of a sequence number");

static const char *TAG = "link";

typedef struct {
    uint16_t len;
    uint8_t *heap; /* NULL: the message is in `data` */
    uint8_t data[TX_INLINE];
} tx_t;

typedef struct {
    uint8_t target;
    uint16_t slot;
} shot_t;

/* One FRAME or STREAM until each of its images is settled; then SHOWN goes out. */
typedef struct {
    bool used;
    bool stream;
    bool shown;
    uint8_t deck;
    uint8_t left;
    uint32_t gen;
    uint32_t id;
    uint64_t t_recv;
    uint64_t t_done;
    const char *error;
} flight_t;

typedef struct {
    int index;
    ws_t ws;
    char url[96];
    volatile bool connected;
    volatile bool over_wlan;
    volatile const char *reconnect; /* why the session ends: set by others, acted on by the link's own task */
    int64_t last_rx_ms;
    int64_t last_ping_ms;
    db_health_t health;
    TaskHandle_t task;
    QueueHandle_t tx;
    bool paired;
    char pair_code[7]; /* shown on the decks only; kept over reconnects until paired */
    uint8_t pair_wrong;
    bool heard;         /* the host sent a message on this connection */
    char close_why[40]; /* the reason in the host's close frame */
    uint16_t close_code;
    volatile bool wants_active; /* sent ROLE 1 on this connection */
    uint32_t claimed;           /* when, in s.claims */
    bool fallback_up;           /* its FALLBACK shows since it dropped as the active host */
    uint32_t credit_bytes;      /* the free cache bytes the last CREDIT told */
    uint32_t input_seq;
    SemaphoreHandle_t flight_lock;
    uint32_t gen;
    flight_t flight[FLIGHT];
    uint8_t fallback_n[DB_MAX_DECKS];
    shot_t fallback[DB_MAX_DECKS][SHOTS_MAX];
    db_device_info_t info; /* the deck a message is for, copied by the link's task */
} link_t;

typedef struct {
    bool used;
    uint8_t format;
    uint16_t w;
    uint16_t h;
    uint32_t off;
    uint32_t len;
} slot_t;

static link_t links[LINKS] EXT_RAM_BSS_ATTR;

static struct {
    db_link_hooks_t hooks;
    volatile int active;
    volatile bool enabled;
    uint32_t inputs_dropped;
    SemaphoreHandle_t role_lock;
    uint32_t claims;
    int pct[DB_MAX_DECKS]; /* the active host's last brightness, -1: none yet */
    struct {               /* what STATE told last, per deck */
        int8_t on;         /* -1: nothing yet */
        int brightness;
    } state[DB_MAX_DECKS];
} s = {.active = -1};

/* One cache for the active host; another host taking over finds it empty. Under `lock`. */
static struct {
    SemaphoreHandle_t lock;
    uint8_t *arena;
    slot_t *slots;
    uint32_t top; /* free from here to the end; holes below go by compact() */
    uint32_t used;
    uint32_t epoch;
    char owner[96];
} cache;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Messages from the deck's tasks go through a sender task per link, so they never wait on the network and a
 * stalled host holds up only its own. False when one of them did not fit. */
static bool send_msg(int link, const void *data, size_t len, TickType_t wait)
{
    bool ok = true;
    for (int i = 0; i < LINKS; i++) {
        if (link != ALL_LINKS && link != i) {
            continue;
        }
        tx_t m = {.len = (uint16_t)len};
        if (len > TX_INLINE && !(m.heap = malloc(len))) {
            ok = false;
            continue;
        }
        memcpy(m.heap ? m.heap : m.data, data, len);
        if (xQueueSend(links[i].tx, &m, wait) != pdTRUE) {
            free(m.heap);
            ok = false;
        }
    }
    return ok;
}

static void tx_task(void *arg)
{
    link_t *l = arg;
    tx_t m;
    for (;;) {
        xQueueReceive(l->tx, &m, portMAX_DELAY);
        if (l->connected && ws_send(&l->ws, WS_OP_BINARY, m.heap ? m.heap : m.data, m.len) != 0) {
            l->reconnect = "send failed";
        }
        free(m.heap);
    }
}

static void send_error(int link, uint8_t deck, const char *code, const char *message)
{
    uint8_t buf[200];
    db_proto_hdr(buf, DB_MSG_ERROR, deck);
    db_cbor_t c;
    db_cbor_begin(&c, buf + DB_PROTO_HDR, sizeof(buf) - DB_PROTO_HDR);
    db_cbor_map(&c, NULL);
    db_cbor_str(&c, "code", code);
    db_cbor_str(&c, "message", message);
    db_cbor_close(&c);
    size_t n = db_cbor_end(&c);
    if (n) {
        send_msg(link, buf, DB_PROTO_HDR + n, 0);
    }
}

static void send_shown(int link, uint8_t deck, bool stream, uint32_t id, uint32_t us)
{
    uint8_t out[DB_SHOWN_LEN];
    db_proto_shown(out, deck, stream, id, us);
    if (!send_msg(link, out, sizeof(out), pdMS_TO_TICKS(20))) { /* the host would wait for it forever */
        links[link].reconnect = "SHOWN lost, send queue full";  /* a new session counts from zero */
    }
}

static int in_flight(link_t *l, int deck)
{
    int n = 0;
    xSemaphoreTake(l->flight_lock, portMAX_DELAY);
    for (int i = 0; i < FLIGHT; i++) {
        n += l->flight[i].used && l->flight[i].deck == deck;
    }
    xSemaphoreGive(l->flight_lock);
    return n;
}

/* Free cache bytes, and the frames this deck takes before a SHOWN, per display. */
static void send_credit(link_t *l, int deck, const db_device_info_t *info)
{
    uint32_t bytes = CACHE_BYTES - cache.used;
    l->credit_bytes = bytes;
    for (uint8_t d = 0; d < info->display_count; d++) {
        uint8_t out[DB_CREDIT_LEN];
        db_proto_credit(out, (uint8_t)deck, d, bytes, (uint8_t)(DECK_FRAMES - in_flight(l, deck)));
        send_msg(l->index, out, sizeof(out), pdMS_TO_TICKS(100));
    }
}

static bool send_state(int link, int deck)
{
    uint8_t buf[48];
    db_proto_hdr(buf, DB_MSG_STATE, (uint8_t)deck);
    db_cbor_t c;
    db_cbor_begin(&c, buf + DB_PROTO_HDR, sizeof(buf) - DB_PROTO_HDR);
    db_cbor_map(&c, NULL);
    db_cbor_bool(&c, "display_on", s.state[deck].on == 1);
    db_cbor_uint(&c, "brightness", (uint64_t)s.state[deck].brightness);
    db_cbor_close(&c);
    return send_msg(link, buf, DB_PROTO_HDR + db_cbor_end(&c), pdMS_TO_TICKS(100));
}

/* HELLO goes out by the link's own task before anything else may: the link is not marked connected yet. */
static int send_hello(link_t *l)
{
    uint8_t *buf = heap_caps_malloc(HELLO_MAX, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return -ENOMEM;
    }
    char id[13];
    db_config_board_id(id);
    db_proto_hdr(buf, DB_MSG_HELLO, DB_PROTO_BOARD);
    db_cbor_t c;
    db_cbor_begin(&c, buf + DB_PROTO_HDR, HELLO_MAX - DB_PROTO_HDR);
    db_cbor_map(&c, NULL);
    db_cbor_uint(&c, "proto", DB_PROTO_VERSION);
    db_cbor_str(&c, "firmware", esp_app_get_description()->version);
    db_cbor_map(&c, "board");
    db_cbor_str(&c, "id", id);
    db_cbor_str(&c, "name", db_config()->name);
    db_cbor_close(&c);
    if (!l->paired) {
        db_cbor_bool(&c, "pairing", true);
    }
    db_cbor_map(&c, "cache");
    db_cbor_uint(&c, "bytes", CACHE_BYTES);
    db_cbor_uint(&c, "slots", CACHE_SLOTS);
    db_cbor_close(&c);
    db_cbor_uint(&c, "cache_epoch", cache.epoch);
    db_cbor_arr(&c, "decks");
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (db_deck_info(d, &l->info)) {
            db_proto_deck(&c, NULL, d, &l->info);
        }
    }
    db_cbor_close(&c);
    db_cbor_close(&c);
    size_t n = db_cbor_end(&c);
    int err = n ? ws_send(&l->ws, WS_OP_BINARY, buf, DB_PROTO_HDR + n) : -EMSGSIZE;
    heap_caps_free(buf);
    return err;
}

/* CREDIT for every deck, and STATE with `state`. By the link's own task. */
static void send_decks(link_t *l, bool state)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (db_deck_info(d, &l->info)) {
            send_credit(l, d, &l->info);
            if (state) {
                send_state(l->index, d);
            }
        }
    }
}

/* The host keeps its PUTs within the last CREDIT: a new one once the free bytes moved by a sixteenth. */
static void credit_again(link_t *l)
{
    uint32_t bytes = CACHE_BYTES - cache.used;
    uint32_t moved = bytes > l->credit_bytes ? bytes - l->credit_bytes : l->credit_bytes - bytes;
    if (moved >= CACHE_BYTES / 16) {
        send_decks(l, false);
    }
}

static void flight_reset(link_t *l)
{
    xSemaphoreTake(l->flight_lock, portMAX_DELAY);
    memset(l->flight, 0, sizeof(l->flight));
    xSemaphoreGive(l->flight_lock);
}

/* A FRAME or STREAM of `n` images, -1 when the deck has DECK_FRAMES in flight already. */
static int flight_open(link_t *l, uint8_t deck, bool stream, uint32_t id, uint8_t n)
{
    xSemaphoreTake(l->flight_lock, portMAX_DELAY);
    int free_at = -1;
    int busy = 0;
    for (int i = 0; i < FLIGHT; i++) {
        busy += l->flight[i].used && l->flight[i].deck == deck;
        free_at = free_at < 0 && !l->flight[i].used ? i : free_at;
    }
    if (busy >= DECK_FRAMES || free_at < 0) {
        xSemaphoreGive(l->flight_lock);
        return -1;
    }
    l->flight[free_at] =
        (flight_t){.used = true,
                   .stream = stream,
                   .deck = deck,
                   .left = n,
                   .gen = l->gen = l->gen % 0xffffff + 1, /* 1 to 2^24 - 1: 0 is the seq of none */
                   .id = id,
                   .t_recv = (uint64_t)esp_timer_get_time()};
    xSemaphoreGive(l->flight_lock);
    return free_at;
}

static uint32_t flight_seq(link_t *l, int at)
{
    return l->flight[at].gen << 8 | (uint32_t)at;
}

static const char *ack_error(db_ack_result_t r)
{
    switch (r) {
    case DB_ACK_REJECTED_RECT:
        return "bad_target";
    case DB_ACK_REJECTED_ROLE:
        return "not_drawing";
    case DB_ACK_NO_DEVICE:
        return "no_deck";
    case DB_ACK_TOO_BIG:
        return "too_big";
    default:
        return "busy";
    }
}

/* One image of a flight is done; the last one sends SHOWN, after an ERROR when one was not shown. */
static void settle(link_t *l, uint32_t seq, db_ack_result_t r, uint64_t t_done, const char *why)
{
    uint32_t at = seq & 0xff;
    if (at >= FLIGHT) {
        return;
    }
    xSemaphoreTake(l->flight_lock, portMAX_DELAY);
    flight_t *f = &l->flight[at];
    if (!f->used || f->gen != seq >> 8) {
        xSemaphoreGive(l->flight_lock);
        return;
    }
    if (r == DB_ACK_SHOWN) {
        f->shown = true;
        f->t_done = t_done > f->t_done ? t_done : f->t_done;
    } else if (r != DB_ACK_SUPERSEDED && !f->error) {
        f->error = why ? why : ack_error(r);
    }
    flight_t done = *f;
    bool last = --f->left == 0;
    f->used = !last;
    xSemaphoreGive(l->flight_lock);
    if (!last) {
        return;
    }
    if (done.error) {
        send_error(l->index, done.deck, done.error,
                   done.stream ? "a STREAM was not shown" : "a FRAME was not shown");
    }
    uint64_t us = done.shown && done.t_done > done.t_recv ? done.t_done - done.t_recv : 0;
    send_shown(l->index, done.deck, done.stream, done.id, us > UINT32_MAX ? UINT32_MAX : (uint32_t)us);
}

static void on_ack(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv, uint64_t t_start,
                   uint64_t t_done, void *arg)
{
    settle(&links[(intptr_t)arg], seq, result, t_done, NULL);
}

static void cache_drop(uint16_t slot)
{
    slot_t *sl = &cache.slots[slot];
    if (sl->used) {
        cache.used -= sl->len;
        sl->used = false;
    }
}

static int by_offset(const void *a, const void *b)
{
    uint32_t x = cache.slots[*(const uint16_t *)a].off;
    uint32_t y = cache.slots[*(const uint16_t *)b].off;
    return x < y ? -1 : x > y;
}

/* Close the holes: every image moves down, in order. */
static void compact(void)
{
    static uint16_t order[CACHE_SLOTS];
    size_t n = 0;
    for (uint16_t i = 0; i < CACHE_SLOTS; i++) {
        if (cache.slots[i].used) {
            order[n++] = i;
        }
    }
    qsort(order, n, sizeof(order[0]), by_offset);
    uint32_t at = 0;
    for (size_t i = 0; i < n; i++) {
        slot_t *sl = &cache.slots[order[i]];
        memmove(cache.arena + at, cache.arena + sl->off, sl->len);
        sl->off = at;
        at += sl->len;
    }
    cache.top = at;
}

/* `slot` is dropped already. NULL when the budget is spent. */
static uint8_t *cache_alloc(uint16_t slot, uint32_t len)
{
    if (len > CACHE_BYTES - cache.used) {
        return NULL;
    }
    if (len > CACHE_BYTES - cache.top) {
        compact();
    }
    slot_t *sl = &cache.slots[slot];
    sl->off = cache.top;
    sl->len = len;
    sl->used = true;
    cache.top += len;
    cache.used += len;
    return cache.arena + sl->off;
}

/* Under role_lock. A new owner finds the cache empty, under a new epoch; the first one finds it empty anyway.
 */
static void cache_take(const char *url)
{
    xSemaphoreTake(cache.lock, portMAX_DELAY);
    if (strcmp(cache.owner, url) != 0) {
        memset(cache.slots, 0, CACHE_SLOTS * sizeof(slot_t));
        cache.top = cache.used = 0;
        cache.epoch += cache.owner[0] != '\0';
        strlcpy(cache.owner, url, sizeof(cache.owner));
        for (int i = 0; i < LINKS; i++) {
            memset(links[i].fallback_n, 0, sizeof(links[i].fallback_n));
        }
        ESP_LOGI(TAG, "cache cleared for %s, epoch %u", url, (unsigned)cache.epoch);
    }
    xSemaphoreGive(cache.lock);
}

/* Under role_lock: the host that asked last, while connected; else the active one stays while its FALLBACK
 * shows. */
static int pick_active(void)
{
    int next = -1;
    for (int i = 0; i < LINKS && s.enabled; i++) {
        link_t *l = &links[i];
        if (l->connected && l->paired && l->wants_active && (next < 0 || l->claimed > links[next].claimed)) {
            next = i;
        }
    }
    int a = s.active;
    return next < 0 && a >= 0 && s.enabled && links[a].fallback_up ? a : next;
}

static void recompute_active(void)
{
    xSemaphoreTake(s.role_lock,
                   portMAX_DELAY); /* every link task calls this; hooks must see changes in order */
    int next = pick_active();
    int was = s.active;
    if (next != was) {
        ESP_LOGI(TAG, "active host %d -> %d", was, next);
        if (was >= 0 && next >= 0) {
            links[was].wants_active = false; /* taken over: it asks again to come back */
            links[was].fallback_up = false;
        }
        if (next >= 0) {
            cache_take(links[next].url);
        }
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            s.pct[d] = -1; /* the new one sends its own */
        }
        s.active = next;
        s.hooks.active_changed(next);
    }
    xSemaphoreGive(s.role_lock);
}

static bool is_active(const link_t *l)
{
    return s.active == l->index && l->connected && l->wants_active;
}

static db_source_t source_of(const link_t *l)
{
    return (db_source_t)(DB_SOURCE_LINK0 + l->index);
}

/* Each (target, slot) of `shots` to deck `deck`, under cache.lock; `at` is the flight, -1 for none. Returns
 * how many went to the deck. */
static int draw(link_t *l, uint8_t deck, const shot_t *shots, int n, int at)
{
    int sent = 0;
    uint32_t seq = at >= 0 ? flight_seq(l, at) : 0;
    uint64_t now = (uint64_t)esp_timer_get_time();
    for (int i = 0; i < n; i++) {
        uint8_t display = 0;
        const db_target_t *t = db_proto_target(&l->info, shots[i].target, &display);
        const slot_t *sl = shots[i].slot < CACHE_SLOTS ? &cache.slots[shots[i].slot] : NULL;
        const char *why = !t                                                            ? "bad_target"
                          : !sl || !sl->used                                            ? "empty_slot"
                          : sl->w != t->rect.w || sl->h != t->rect.h                    ? "bad_size"
                          : !db_proto_format_ok(&l->info.displays[display], sl->format) ? "bad_format"
                                                                                        : NULL;
        uint8_t *data = why ? NULL : heap_caps_malloc(sl->len, MALLOC_CAP_SPIRAM);
        if (!data) {
            if (at < 0) {
                ESP_LOGW(TAG, "host %d: fallback on deck %d: %s", l->index, deck + 1,
                         why ? why : "no memory");
            }
            settle(l, seq, DB_ACK_BUSY, 0, why); /* seq 0 is no flight */
            continue;
        }
        memcpy(data, cache.arena + sl->off, sl->len);
        db_deck_submit(deck, source_of(l), display, t->rect, seq, data, sl->len, now);
        sent++;
    }
    return sent;
}

/* The deck in a message header, copied into l->info. */
static bool deck_ok(link_t *l, uint8_t deck)
{
    return deck < DB_MAX_DECKS && db_deck_info(deck, &l->info);
}

static int read_shots(link_t *l, shot_t *shots, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        uint8_t b[3];
        int err = ws_read(&l->ws, b, sizeof(b));
        if (err) {
            return err;
        }
        shots[i] = (shot_t){.target = b[0], .slot = db_get_u16(b + 1)};
    }
    return 0;
}

static int on_frame(link_t *l, uint8_t deck, uint64_t len)
{
    uint8_t h[6];
    if (len < sizeof(h)) {
        send_error(l->index, deck, "bad_message", "FRAME too short");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, h, sizeof(h));
    uint32_t id = db_get_u32(h);
    uint16_t n = db_get_u16(h + 4);
    len -= sizeof(h);
    const char *why = err                                ? NULL
                      : len != 3ull * n || n > SHOTS_MAX ? "bad_message"
                      : !is_active(l)                    ? "standby"
                      : !deck_ok(l, deck)                ? "no_deck"
                                                         : NULL;
    int at = err || why || n == 0 ? -1 : flight_open(l, deck, false, id, (uint8_t)n);
    if (err || why || at < 0) {
        if (!err && n) {
            send_error(l->index, deck, why ? why : "busy", "FRAME not shown");
        }
        if (!err) {
            send_shown(l->index, deck, false, id, 0);
        }
        return err ? err : ws_skip(&l->ws, len);
    }
    shot_t shots[SHOTS_MAX];
    err = read_shots(l, shots, n);
    if (err) {
        return err;
    }
    xSemaphoreTake(cache.lock, portMAX_DELAY);
    draw(l, deck, shots, n, at);
    xSemaphoreGive(cache.lock);
    return 0;
}

static int on_stream(link_t *l, uint8_t deck, uint64_t len)
{
    uint8_t h[6];
    if (len < sizeof(h)) {
        send_error(l->index, deck, "bad_message", "STREAM too short");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, h, sizeof(h));
    if (err) {
        return err;
    }
    uint32_t seq = db_get_u32(h + 1);
    len -= sizeof(h);
    uint8_t display = 0;
    const db_target_t *t = NULL;
    const char *why = !is_active(l)                                           ? "standby"
                      : !deck_ok(l, deck)                                     ? "no_deck"
                      : !(t = db_proto_target(&l->info, h[0], &display))      ? "bad_target"
                      : !db_proto_format_ok(&l->info.displays[display], h[5]) ? "bad_format"
                      : len == 0 || len > DB_PROTO_IMAGE_MAX                  ? "too_big"
                                                                              : NULL;
    int at = why ? -1 : flight_open(l, deck, true, seq, 1);
    uint8_t *data = at < 0 ? NULL : heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
    if (!data) {
        if (at >= 0) {
            settle(l, flight_seq(l, at), DB_ACK_BUSY, 0, NULL);
        } else {
            send_error(l->index, deck, why ? why : "busy", "STREAM not shown");
            send_shown(l->index, deck, true, seq, 0);
        }
        return ws_skip(&l->ws, len);
    }
    err = ws_read(&l->ws, data, (size_t)len);
    if (err) {
        heap_caps_free(data);
        return err;
    }
    db_deck_submit(deck, source_of(l), display, t->rect, flight_seq(l, at), data, (size_t)len,
                   (uint64_t)esp_timer_get_time());
    return 0;
}

/* The image goes from the socket straight into its slot. */
static int on_put(link_t *l, uint8_t deck, uint64_t len)
{
    uint8_t h[7];
    if (len < sizeof(h) || !is_active(l)) {
        send_error(l->index, deck, len < sizeof(h) ? "bad_message" : "standby", "PUT not stored");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, h, sizeof(h));
    if (err) {
        return err;
    }
    uint16_t slot = db_get_u16(h);
    len -= sizeof(h);
    if (slot >= CACHE_SLOTS || h[2] > DB_IMG_PNG || len == 0 || len > DB_PROTO_IMAGE_MAX) {
        send_error(l->index, deck, slot >= CACHE_SLOTS ? "bad_slot" : "bad_message", "PUT not stored");
        return ws_skip(&l->ws, len);
    }
    xSemaphoreTake(cache.lock, portMAX_DELAY);
    uint32_t old = cache.slots[slot].used ? cache.slots[slot].len : 0;
    uint8_t *dst = NULL;
    if (len <= CACHE_BYTES - (cache.used - old)) { /* else the old picture stays */
        cache_drop(slot);
        dst = cache_alloc(slot, (uint32_t)len);
    }
    if (!dst) {
        xSemaphoreGive(cache.lock);
        send_error(l->index, deck, "cache_full", "PUT does not fit the cache budget");
        return ws_skip(&l->ws, len);
    }
    slot_t *sl = &cache.slots[slot];
    sl->format = h[2];
    sl->w = db_get_u16(h + 3);
    sl->h = db_get_u16(h + 5);
    err = ws_read(&l->ws, dst, (size_t)len);
    if (err) {
        cache_drop(slot);
    }
    xSemaphoreGive(cache.lock);
    credit_again(l);
    return err;
}

static int on_evict(link_t *l, uint8_t deck, uint64_t len)
{
    uint8_t b[2];
    if (len < 2 || !is_active(l)) {
        send_error(l->index, deck, len < 2 ? "bad_message" : "standby", "EVICT ignored");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, b, 2);
    uint16_t n = db_get_u16(b);
    len -= 2;
    if (err || (n == 0xffff ? len != 0 : len != 2ull * n)) {
        if (!err) {
            send_error(l->index, deck, "bad_message", "EVICT ignored");
        }
        return err ? err : ws_skip(&l->ws, len);
    }
    xSemaphoreTake(cache.lock, portMAX_DELAY);
    for (uint16_t i = 0; n == 0xffff ? i < CACHE_SLOTS : i < n; i++) {
        uint16_t slot = i;
        if (n != 0xffff && (err = ws_read(&l->ws, b, 2)) == 0) {
            slot = db_get_u16(b);
        }
        if (err) {
            break;
        }
        if (slot < CACHE_SLOTS) {
            cache_drop(slot);
        }
    }
    if (cache.used == 0) {
        cache.top = 0;
    }
    xSemaphoreGive(cache.lock);
    credit_again(l);
    return err;
}

static int on_fallback(link_t *l, uint8_t deck, uint64_t len)
{
    uint8_t b[2];
    if (len < 2 || !is_active(l) || deck >= DB_MAX_DECKS) {
        send_error(l->index, deck, !is_active(l) ? "standby" : "bad_message", "FALLBACK ignored");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, b, 2);
    uint16_t n = db_get_u16(b);
    len -= 2;
    if (err || len != 3ull * n || n > SHOTS_MAX) {
        if (!err) {
            send_error(l->index, deck, "bad_message", "FALLBACK ignored");
        }
        return err ? err : ws_skip(&l->ws, len);
    }
    shot_t shots[SHOTS_MAX];
    err = read_shots(l, shots, n);
    uint8_t display;
    for (uint16_t i = 0; !err && i < n; i++) {
        if (!deck_ok(l, deck) || !db_proto_target(&l->info, shots[i].target, &display) ||
            shots[i].slot >= CACHE_SLOTS) {
            send_error(l->index, deck,
                       !deck_ok(l, deck)              ? "no_deck"
                       : shots[i].slot >= CACHE_SLOTS ? "bad_slot"
                                                      : "bad_target",
                       "FALLBACK ignored");
            return 0;
        }
    }
    if (!err) {
        xSemaphoreTake(cache.lock, portMAX_DELAY);
        memcpy(l->fallback[deck], shots, n * sizeof(shots[0]));
        l->fallback_n[deck] = (uint8_t)n;
        xSemaphoreGive(cache.lock);
    }
    return err;
}

/* The active host dropped: what it left for this shows, and stays until another host takes over. */
static void show_fallback(link_t *l)
{
    xSemaphoreTake(cache.lock, portMAX_DELAY);
    for (uint8_t d = 0; d < DB_MAX_DECKS; d++) {
        if (l->fallback_n[d] && db_deck_info(d, &l->info) &&
            draw(l, d, l->fallback[d], l->fallback_n[d], -1)) {
            l->fallback_up = true;
        }
    }
    xSemaphoreGive(cache.lock);
}

static void set_brightness(link_t *l, uint8_t deck, uint8_t pct)
{
    pct = pct > 100 ? 100 : pct;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (deck == d || deck == DB_PROTO_BOARD) {
            s.pct[d] = pct;
            if (db_deck_writer(d) == source_of(l)) { /* else after the menu */
                db_deck_set_brightness(d, pct);
            }
        }
    }
}

static void new_pair_code(link_t *l)
{
    snprintf(l->pair_code, sizeof(l->pair_code), "%06u", (unsigned)(esp_random() % 1000000));
    l->pair_wrong = 0;
#if CONFIG_IDF_TARGET_LINUX /* the simulators' tests read it here; a board shows it on the deck only */
    ESP_LOGI(TAG, "host %d pairing code %s", l->index, l->pair_code);
#endif
}

/* The ERROR goes out before the connection closes, so not through the queue. */
static void refuse_pairing(link_t *l, const char *code, const char *message)
{
    uint8_t buf[160];
    db_proto_hdr(buf, DB_MSG_ERROR, DB_PROTO_BOARD);
    db_cbor_t c;
    db_cbor_begin(&c, buf + DB_PROTO_HDR, sizeof(buf) - DB_PROTO_HDR);
    db_cbor_map(&c, NULL);
    db_cbor_str(&c, "code", code);
    db_cbor_str(&c, "message", message);
    db_cbor_close(&c);
    ws_send(&l->ws, WS_OP_BINARY, buf, DB_PROTO_HDR + db_cbor_end(&c));
    l->reconnect = message;
}

/* The host no longer knows our token (close 4001, or 401 on the upgrade): pair again. The pin stays, so only
 * the same host can. */
static void forget_token(link_t *l)
{
    db_config_t *c = malloc(sizeof(*c));
    if (!c) {
        return; /* tried again on the next refusal */
    }
    db_config_edit_begin();
    db_config_copy(c);
    c->server_tokens[l->index][0] = '\0';
    esp_err_t err = db_config_save(c);
    db_config_edit_end();
    free(c);
    ESP_LOGW(TAG, "host %d refused the token, pairing again%s", l->index, err == ESP_OK ? "" : ": not saved");
}

/* A token goes into a header line: printable ASCII only. */
static bool token_ok(const char *t)
{
    size_t n = strlen(t);
    for (const char *c = t; *c; c++) {
        if (*c <= ' ' || *c > '~') {
            return false;
        }
    }
    return n > 0 && n < sizeof(db_config()->server_tokens[0]);
}

/* The admin confirmed the code: token and certificate are kept, and the next connection uses them. */
static int on_paired(link_t *l, uint64_t len)
{
    uint8_t buf[PAIRED_MAX];
    if (len > sizeof(buf) || l->paired) {
        send_error(l->index, DB_PROTO_BOARD, "bad_message", l->paired ? "already paired" : "PAIRED too long");
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, buf, (size_t)len);
    if (err) {
        return err;
    }
    char token[64];
    char code[8];
    if (!db_cbor_text(buf, (size_t)len, "token", token, sizeof(token)) || !token_ok(token)) {
        send_error(l->index, DB_PROTO_BOARD, "bad_message",
                   "PAIRED needs a token of up to 47 printable ASCII characters");
        return 0;
    }
    if (!db_cbor_text(buf, (size_t)len, "code", code, sizeof(code)) || strcmp(code, l->pair_code) != 0) {
        if (++l->pair_wrong >= 5) { /* guessing gets no further than five tries per code */
            new_pair_code(l);
            s.hooks.pair_code(l->index, l->pair_code);
        }
        refuse_pairing(l, "pair_code", "not the code the deck shows");
        return 0;
    }
    db_config_t *c = malloc(sizeof(*c));
    if (!c) {
        refuse_pairing(l, "pair_failed", "no memory to save the pairing");
        return 0;
    }
    db_config_edit_begin();
    db_config_copy(c);
    char *url = db_config_server(c, l->index);
    err = -EINVAL; /* the URL in the config changed meanwhile */
    if (!url[0] || strcmp(url, l->url) == 0) {
        strlcpy(url, l->url, sizeof(c->servers_first[0]));
        strlcpy(c->server_tokens[l->index], token, sizeof(c->server_tokens[0]));
        if (strncmp(l->url, "wss://", 6) == 0) {
            for (int i = 0; i < 32; i++) {
                snprintf(c->server_pins[l->index] + 2 * i, 3, "%02x", l->ws.cert[i]);
            }
        }
        err = db_config_save(c) == ESP_OK ? 0 : -EIO;
    }
    db_config_edit_end();
    free(c);
    if (err) {
        ESP_LOGW(TAG, "host %d: pairing not saved: %s", l->index, strerror(-err));
        refuse_pairing(l, "pair_failed", "the pairing could not be saved");
        return 0;
    }
    ESP_LOGI(TAG, "host %d paired", l->index);
    l->pair_code[0] = '\0';
    l->reconnect = "paired";
    return 0;
}

/* 1 with the byte in *v, 0 when the message was not one byte, or a negative errno. */
static int read_u8(link_t *l, uint8_t deck, uint64_t len, uint8_t *v, const char *what)
{
    if (len != 1) {
        send_error(l->index, deck, "bad_message", what);
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, v, 1);
    return err ? err : 1;
}

static int on_message(link_t *l, uint64_t len)
{
    uint8_t h[DB_PROTO_HDR];
    l->heard = true;
    if (len < sizeof(h)) {
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, h, sizeof(h));
    if (err) {
        return err;
    }
    len -= sizeof(h);
    uint8_t type = h[0];
    uint8_t deck = h[1];
    if (type == DB_MSG_PAIRED) {
        return on_paired(l, len);
    }
    if (!l->paired && type <= DB_MSG_REBOOT) {
        send_error(l->index, deck, "unpaired", "waiting for PAIRED");
        return ws_skip(&l->ws, len);
    }
    uint8_t v = 0;
    switch (type) {
    case DB_MSG_PUT:
        return on_put(l, deck, len);
    case DB_MSG_EVICT:
        return on_evict(l, deck, len);
    case DB_MSG_FRAME:
        return on_frame(l, deck, len);
    case DB_MSG_STREAM:
        return on_stream(l, deck, len);
    case DB_MSG_FALLBACK:
        return on_fallback(l, deck, len);
    case DB_MSG_ROLE:
        err = read_u8(l, deck, len, &v, "ROLE is one byte");
        if (err == 1) {
            l->wants_active = v == 1;
            l->fallback_up = false;
            l->claimed = v == 1 ? ++s.claims : l->claimed;
            recompute_active();
            if (is_active(l)) {
                send_decks(l, false); /* the cache may have been emptied for it */
            }
        }
        return err < 0 ? err : 0;
    case DB_MSG_BRIGHTNESS:
        err = read_u8(l, deck, len, &v, "BRIGHTNESS is one byte");
        if (err == 1 && is_active(l)) {
            set_brightness(l, deck, v);
        }
        return err < 0 ? err : 0;
    case DB_MSG_TIME: {
        uint8_t t[8];
        if (len != sizeof(t)) {
            send_error(l->index, deck, "bad_message", "TIME is 8 bytes");
            return ws_skip(&l->ws, len);
        }
        return ws_read(&l->ws, t, sizeof(t)); /* INPUT keeps the bridge's own clock: the host's could jump */
    }
    case DB_MSG_REBOOT:
        if (is_active(l)) {
            ESP_LOGW(TAG, "host %d asked for a restart", l->index);
            db_ota_restart(200);
        } else {
            send_error(l->index, deck, "standby", "only the active host may restart the bridge");
        }
        return ws_skip(&l->ws, len);
    default:
        send_error(l->index, deck, "unknown_type", "unknown message type");
        return ws_skip(&l->ws, len);
    }
}

/* One frame. Returns 0 to go on, or why the session ended. Fragmented messages are not used and are dropped.
 */
static int read_frame(link_t *l)
{
    uint8_t op;
    bool fin;
    uint64_t len;
    int err = ws_read_header(&l->ws, &op, &fin, &len);
    if (err) {
        return err;
    }
    l->last_rx_ms = now_ms();
    if (!fin || op == WS_OP_CONT) {
        return ws_skip(&l->ws, len);
    }
    switch (op) {
    case WS_OP_BINARY:
        return on_message(l, len);
    case WS_OP_PING: {
        uint8_t payload[125];
        if (len > sizeof(payload)) {
            return -EPROTO;
        }
        err = ws_read(&l->ws, payload, (size_t)len);
        return err ? err : ws_send(&l->ws, WS_OP_PONG, payload, (size_t)len);
    }
    case WS_OP_CLOSE: {
        uint8_t payload[125] = {0};
        err = len <= sizeof(payload) ? ws_read(&l->ws, payload, (size_t)len) : ws_skip(&l->ws, len);
        if (!err && len >= 2 && len <= sizeof(payload)) { /* a code, then the reason */
            l->close_code = (uint16_t)(payload[0] << 8 | payload[1]);
            snprintf(l->close_why, sizeof(l->close_why), "%.*s", (int)len - 2, (const char *)payload + 2);
        }
        ws_send(&l->ws, WS_OP_CLOSE, NULL, 0);
        return -ECONNRESET;
    }
    default: /* text and pongs */
        return ws_skip(&l->ws, len);
    }
}

/* true when the host went silent: the path may be dead, not only the host */
static bool session(link_t *l)
{
    l->last_rx_ms = l->last_ping_ms = now_ms();
    for (;;) {
        int err = read_frame(l);
        int64_t now = now_ms();
        if (l->reconnect) {
            ESP_LOGI(TAG, "host %d: %s", l->index, (const char *)l->reconnect);
            l->reconnect = NULL;
            return false;
        }
        if (err == -EAGAIN || err == 0) {
            if (now - l->last_rx_ms > SILENT_LIMIT_MS) {
                ESP_LOGW(TAG, "host %d silent for %d s", l->index, SILENT_LIMIT_MS / 1000);
                return true;
            }
            if (now - l->last_ping_ms > PING_AFTER_MS) {
                l->last_ping_ms = now;
                ws_send(&l->ws, WS_OP_PING, NULL, 0);
            }
            continue;
        }
        char n[12];
        snprintf(n, sizeof(n), "%d", l->index);
        db_log_event(TAG, DB_EV_SERVER_LOST, n, strerror(-err));
        return false;
    }
}

/*
 * Every connection is bound to one interface. Routing cannot be trusted to pick: with LAN and WLAN in the
 * same subnet lwIP takes the first matching interface, and a LAN can keep its link while it drops everything
 * (a switch port that stops forwarding). So LAN first; after a timeout or an unreachable network WLAN, and
 * back.
 */
static void link_task(void *arg)
{
    link_t *l = arg;
    bool via_wlan = false;
    int logged_err[2] = {0}; /* per LAN/WLAN, which alternate: a host that stays away logs once per error */
    uint32_t wait_ms = RETRY_MS;
    char failed[sizeof(l->url)] = "";
    for (;;) {
        if (!l->url[0]) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RETRY_MS));
            continue;
        }
        char ifname[6];
        char url[sizeof(l->url)];
        l->reconnect = NULL; /* asked for while not connected: this connect takes the new address anyway */
        strlcpy(url, l->url, sizeof(url)); /* discover() may rewrite l->url meanwhile */
        db_net_status_t n;
        db_net_status(&n);
        if (!via_wlan && n.lan_link && !n.lan_up && esp_timer_get_time() < BOOT_LAN_WAIT_US) {
            vTaskDelay(pdMS_TO_TICKS(250)); /* the cable is in and DHCP is on its way: no detour over WLAN */
            continue;
        }
        bool wlan = via_wlan;
        bool bound = db_net_pick(&wlan, ifname);
        const char *over = bound ? (wlan ? "WLAN" : "LAN") : "the default route";
        /* the token only goes to the host it was paired with, never to one that announced itself */
        const db_config_t *c = db_config();
        bool set = strcmp(url, db_config_server_of(c, l->index)) == 0;
        char headers[96] = "";
        if (set && c->server_tokens[l->index][0]) {
            snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", c->server_tokens[l->index]);
        }
        uint8_t pin[32];
        bool pinned = set && db_config_pin(c->server_pins[l->index], pin);
        l->paired = headers[0];
        l->ws.any_cert = !l->paired; /* the admin vouches for this one with the code, and it is pinned then */
        int err = ws_connect(&l->ws, url, "deckassistant.v2", l->paired ? headers : NULL,
                             bound ? ifname : NULL, CONNECT_TIMEOUT, pinned ? pin : NULL);
        if (!err) {
            if (!l->pair_code[0]) {
                new_pair_code(l);
            }
            l->wants_active = false;
            l->input_seq = 0;
            flight_reset(l);
            err = send_hello(l);
            if (err) {
                ws_close(&l->ws);
            }
        }
        if (err == -EACCES && l->paired) {
            forget_token(l);
        }
        if (err) {
            db_health_fail(&l->health, l->ws.fail ? l->ws.fail : strerror(-err));
            if (err != logged_err[wlan]) {
                logged_err[wlan] = err;
                ESP_LOGW(TAG, "host %d over %s: %s", l->index, over, strerror(-err));
            }
            /* A refused connection proves the path works and only the host is missing, so the next try goes
             * over LAN. */
            bool path_ok = err == -ECONNREFUSED || err == -ECONNRESET;
            via_wlan = path_ok ? false : !wlan;
            if (strcmp(failed, url) != 0) {
                strlcpy(failed, url, sizeof(failed));
                wait_ms = RETRY_MS;
            }
            bool tried_both = wlan || path_ok;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tried_both ? wait_ms : 200)); /* or a retry now */
            if (tried_both) {
                wait_ms = wait_ms * 2 < RETRY_MAX_MS ? wait_ms * 2 : RETRY_MAX_MS;
            }
            continue;
        }
        failed[0] = '\0';
        logged_err[0] = logged_err[1] = 0;
        char idx[12];
        snprintf(idx, sizeof(idx), "%d", l->index);
        db_log_event(TAG, DB_EV_SERVER_CONNECTED, idx, over, l->url);
        l->over_wlan = bound && wlan;
        db_health_ok(&l->health, l->over_wlan);
        l->connected = true;
        send_decks(l, true);
        db_log_mark(DB_MARK_SERVER);
        if (!l->paired) {
            s.hooks.pair_code(l->index, l->pair_code);
        }
        l->heard = false;
        l->close_why[0] = '\0';
        l->close_code = 0;
        /* silent over LAN: a LAN that keeps its link but drops everything would cost a connect timeout */
        via_wlan = session(l) && !l->over_wlan;
        wait_ms = l->heard ? RETRY_MS : wait_ms;
        l->connected = false;
        if (!l->paired) {
            s.hooks.pair_code(l->index, NULL);
        }
        db_health_fail(&l->health, l->close_why[0] ? l->close_why : "connection lost");
        ws_close(&l->ws);
        flight_reset(l);
        if (s.active == l->index) {
            show_fallback(l);
        }
        recompute_active();
        if (l->close_code == 4001 && l->paired) {
            forget_token(l);
        }
        if (!l->heard) { /* turned away at once, a refused token perhaps: no reconnecting five times a second
                          */
            ESP_LOGW(TAG, "host %d closed without a message: %s", l->index,
                     l->close_why[0] ? l->close_why : "-");
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
            wait_ms = wait_ms * 2 < RETRY_MAX_MS ? wait_ms * 2 : RETRY_MAX_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void on_input(int deck, const db_input_t *ev)
{
    int a = s.active;
    if (a < 0 || !is_active(&links[a]) || db_deck_writer(deck) != source_of(&links[a]) ||
        !db_deck_input_real_for(links[a].ws.peer)) {
        return;
    }
    uint8_t out[DB_INPUT_LEN];
    db_proto_input(out, (uint8_t)deck, links[a].input_seq++, ev->t_us, ev);
    if (!send_msg(a, out, sizeof(out), pdMS_TO_TICKS(20))) {
        s.inputs_dropped++;
    }
}

static void on_device(int deck, const db_device_info_t *info)
{
    s.state[deck].on = -1;
    if (!info) {
        uint8_t out[DB_PROTO_HDR];
        db_proto_hdr(out, DB_MSG_DECK_REMOVED, (uint8_t)deck);
        send_msg(ALL_LINKS, out, sizeof(out), pdMS_TO_TICKS(100));
        return;
    }
    uint8_t *buf = heap_caps_malloc(HELLO_MAX, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "no memory for DECK_ADDED of deck %d", deck + 1);
        return;
    }
    db_proto_hdr(buf, DB_MSG_DECK_ADDED, (uint8_t)deck);
    db_cbor_t c;
    db_cbor_begin(&c, buf + DB_PROTO_HDR, HELLO_MAX - DB_PROTO_HDR);
    db_proto_deck(&c, NULL, deck, info);
    size_t n = db_cbor_end(&c);
    if (n) {
        send_msg(ALL_LINKS, buf, DB_PROTO_HDR + n, pdMS_TO_TICKS(100));
        for (int i = 0; i < LINKS; i++) {
            send_credit(&links[i], deck, info);
        }
    }
    heap_caps_free(buf);
}

/* Display on or off and the brightness it shows, to every host when it changes. */
static void watch_state(void)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (!db_deck_present(d)) {
            continue;
        }
        int b = db_deck_shown(d);
        int8_t on = (int8_t)(!db_deck_screen_is_off() && b != 0);
        b = b < 0 ? 0 : b;
        int8_t was_on = s.state[d].on;
        int was = s.state[d].brightness;
        if (on != was_on || b != was) {
            s.state[d].on = on;
            s.state[d].brightness = b;
            if (!send_state(ALL_LINKS, d)) { /* again on the next tick */
                s.state[d].on = was_on;
                s.state[d].brightness = was;
            }
        }
    }
}

/* Links without a URL in the config take hosts that announce _deckassistant._tcp. */
static void discover(void)
{
    bool room = false;
    for (int i = 0; i < LINKS; i++) {
        room |= !db_config_server_of(db_config(), i)[0] && !links[i].connected;
    }
    mdns_result_t *results = NULL;
    if (!room || mdns_query_ptr("_deckassistant", "_tcp", 2000, 4, &results) != ESP_OK) {
        return;
    }
    for (mdns_result_t *r = results; r; r = r->next) {
        char url[sizeof(links[0].url)];
        const mdns_ip_addr_t *a = r->addr;
        while (a && a->addr.type != ESP_IPADDR_TYPE_V4) {
            a = a->next;
        }
        if (!a) {
            continue;
        }
        snprintf(url, sizeof(url), "wss://" IPSTR ":%u/surface", IP2STR(&a->addr.u_addr.ip4), r->port);
        bool known = false;
        for (int i = 0; i < LINKS; i++) {
            known |= strcmp(links[i].url, url) == 0;
        }
        for (int i = 0; i < LINKS && !known; i++) {
            link_t *l = &links[i];
            if (!db_config_server_of(db_config(), i)[0] && !l->connected) {
                strlcpy(l->url, url, sizeof(l->url));
                ESP_LOGI(TAG, "found host %s (%s)", url, r->instance_name ? r->instance_name : "?");
                db_link_retry(i);
                known = true;
            }
        }
    }
    mdns_query_results_free(results);
}

/* A link on WLAN goes back to LAN as soon as the host answers there again: after a start, when WLAN was up
 * first, or when a dead switch port works again. The probe is one TCP connect, so a dead LAN never wins. */
static void back_to_lan(void)
{
    char lan[6];
    if (!db_net_ifname(false, lan)) {
        return;
    }
    for (int i = 0; i < LINKS; i++) {
        link_t *l = &links[i];
        char url[sizeof(l->url)];
        strlcpy(url, l->url, sizeof(url));
        if (l->connected && l->over_wlan && ws_probe(url, lan, PROBE_TIMEOUT) == 0) {
            l->over_wlan = false;
            l->reconnect = "LAN is up, moving from WLAN";
        }
    }
}

static void supervisor_task(void *arg)
{
    for (unsigned tick = 0;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        watch_state();
        if (tick % (s.active < 0 ? DISCOVER_EVERY : 12 * DISCOVER_EVERY) == 0) {
            discover();
            back_to_lan();
        }
    }
}

esp_err_t db_link_start(const db_link_hooks_t *hooks)
{
    s.hooks = *hooks;
    s.role_lock = xSemaphoreCreateMutex();
    cache.lock = xSemaphoreCreateMutex();
    /* reserved at the start, before fragmentation could take an update's 4 MB block */
    cache.arena = heap_caps_malloc(CACHE_BYTES, MALLOC_CAP_SPIRAM);
    cache.slots = heap_caps_calloc(CACHE_SLOTS, sizeof(slot_t), MALLOC_CAP_SPIRAM);
    cache.epoch = esp_random(); /* a host must not take a restarted bridge's empty cache for its own */
    if (!s.role_lock || !cache.lock || !cache.arena || !cache.slots) {
        return ESP_ERR_NO_MEM;
    }
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        s.pct[d] = -1;
        s.state[d].on = -1;
    }
    db_deck_listener_t dl = {.input = on_input, .device_changed = on_device, .external = true};
    db_deck_add_listener(&dl);
    for (int i = 0; i < LINKS; i++) {
        link_t *l = &links[i];
        l->index = i;
        l->flight_lock = xSemaphoreCreateMutex();
        l->tx = xQueueCreate(TX_QUEUE, sizeof(tx_t));
        if (!l->flight_lock || !l->tx || ws_init(&l->ws)) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(l->url, db_config_server_of(db_config(), i), sizeof(l->url));
        db_deck_set_ack_handler(source_of(l), on_ack, (void *)(intptr_t)i);
        char name[12];
        snprintf(name, sizeof(name), "link%d", i);
        char tx_name[12];
        snprintf(tx_name, sizeof(tx_name), "link%d_tx", i);
        if (xTaskCreatePinnedToCore(link_task, name, 8192, l, 6, &l->task, 0) != pdPASS ||
            xTaskCreatePinnedToCore(tx_task, tx_name, 3072, l, 7, NULL, 0) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    db_link_reload(); /* takes note of the pins, before any link is up */
    bool ok = xTaskCreatePinnedToCore(supervisor_task, "link_sup", 4096, NULL, 3, NULL, 0) == pdPASS;
    return ok ? ESP_OK : ESP_ERR_NO_MEM;
}

void db_link_reload(void)
{
    /* only what the config changed: a host found by mDNS keeps its URL while the config stays empty */
    static char applied[LINKS][sizeof(links[0].url)];
    static char applied_pin[LINKS][sizeof(db_config()->server_pins[0])];
    static char applied_token[LINKS][sizeof(db_config()->server_tokens[0])];
    const db_config_t *c = db_config();
    for (int i = 0; i < LINKS; i++) {
        const char *want = db_config_server_of(c, i);
        if (strcmp(c->server_pins[i], applied_pin[i]) != 0 ||
            strcmp(c->server_tokens[i], applied_token[i]) != 0) {
            strlcpy(applied_pin[i], c->server_pins[i], sizeof(applied_pin[i]));
            strlcpy(applied_token[i], c->server_tokens[i], sizeof(applied_token[i]));
            if (links[i].connected) { /* the next connect takes the new ones anyway */
                links[i].reconnect = "pairing changed";
            }
        }
        if (strcmp(want, applied[i]) == 0) {
            continue;
        }
        strlcpy(applied[i], want, sizeof(applied[i]));
        if (want[0] && strcmp(want, links[i].url) != 0) {
            strlcpy(links[i].url, want, sizeof(links[i].url));
            links[i].reconnect = "address changed";
            db_link_retry(i);
        } else if (!want[0] && links[i].connected) {
            links[i].url[0] = '\0';
            links[i].reconnect = "address removed";
        }
    }
}

void db_link_set_enabled(bool enabled)
{
    s.enabled = enabled;
    recompute_active();
}

int db_link_deck(void)
{
    int first = db_deck_first();
    return first < 0 ? 0 : first;
}

void db_link_brightness_again(int deck)
{
    if (s.active >= 0 && s.pct[deck] >= 0) {
        db_deck_set_brightness(deck, (uint8_t)s.pct[deck]);
    }
}

uint32_t db_link_inputs_dropped(void)
{
    return s.inputs_dropped;
}

bool db_link_connected(int index)
{
    return index >= 0 && index < LINKS && links[index].connected;
}

db_health_t db_link_health(int index)
{
    return links[index].health;
}

void db_link_retry(int index)
{
    if (index >= 0 && index < LINKS && links[index].task) {
        xTaskNotifyGive(links[index].task);
    }
}

const char *db_link_url(int index)
{
    return index >= 0 && index < LINKS ? links[index].url : "";
}
