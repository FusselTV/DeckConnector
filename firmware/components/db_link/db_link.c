#include "db_link.h"
#include "db_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_board.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_proto.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "ws.h"

#define TX_INLINE        32
#define TX_QUEUE         64
#define TEXT_MAX         2048
#define IMAGE_MAX        (1024 * 1024)
#define STATUS_PERIOD    10000
#define PROBE_TIMEOUT    1000
#define BOOT_LAN_WAIT_US (30LL * 1000000)
#define DISCOVER_EVERY   5000
#define RETRY_MS         2000
#define RETRY_MAX_MS     30000
#define CONNECT_TIMEOUT  3000
#define PING_AFTER_MS    2000
#define SILENT_LIMIT_MS  6000
#define ALL_LINKS        (-1)

_Static_assert(DB_MAX_SERVERS == DB_SOURCE_LINKS, "one deck source per server");

static const char *TAG = "link";

typedef struct {
    int8_t link;
    uint8_t op;
    uint16_t len;
    char *heap; /* NULL: payload is in `data` */
    uint8_t data[TX_INLINE];
} tx_t;

typedef struct {
    int index;
    ws_t ws;
    char url[96];
    volatile bool connected;
    volatile bool over_wlan;
    volatile const char *reconnect; /* why the session ends: set by others, acted on by the link's own task */
    int64_t last_rx_ms;
    int64_t last_ping_ms;
    char text[TEXT_MAX];
    db_health_t health;
    TaskHandle_t task;
} link_t;

static struct {
    db_link_hooks_t hooks;
    link_t links[DB_MAX_SERVERS];
    volatile int active;
    volatile bool enabled;
    QueueHandle_t tx;
    uint32_t inputs_dropped;
    SemaphoreHandle_t role_lock;
    char headers[96];
} s = {.active = -1};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* An ack or time reply in a full queue is dropped; the server times out or asks again. */
static bool send_inline(int link, uint8_t op, const void *data, size_t len, TickType_t wait)
{
    tx_t m = {.link = (int8_t)link, .op = op, .len = (uint16_t)len};
    memcpy(m.data, data, len);
    return xQueueSend(s.tx, &m, wait) == pdTRUE;
}

static void send_text(int link, const char *json, size_t len)
{
    if (len == 0) {
        return;
    }
    tx_t m = {.link = (int8_t)link, .op = WS_OP_TEXT, .len = (uint16_t)len, .heap = malloc(len)};
    if (!m.heap) {
        return;
    }
    memcpy(m.heap, json, len);
    if (xQueueSend(s.tx, &m, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(m.heap);
    }
}

/* Sending happens in one task, so acks from the deck writer never wait on the network. */
static void tx_task(void *arg)
{
    tx_t m;
    for (;;) {
        xQueueReceive(s.tx, &m, portMAX_DELAY);
        for (int i = 0; i < DB_MAX_SERVERS; i++) {
            link_t *l = &s.links[i];
            if ((m.link == ALL_LINKS || m.link == i) && l->connected &&
                ws_send(&l->ws, m.op, m.heap ? (const void *)m.heap : m.data, m.len) != 0) {
                l->reconnect = "send failed";
            }
        }
        free(m.heap);
    }
}

void db_link_broadcast(const char *json)
{
    send_text(ALL_LINKS, json, strlen(json));
}

static void send_device(int link)
{
    db_device_info_t *info = malloc(sizeof(*info));
    char *buf = malloc(4096);
    if (info && buf) {
        bool present = db_deck_info(info);
        send_text(link, buf, db_proto_device_json(present ? info : NULL, buf, 4096));
    }
    free(buf);
    free(info);
}

static void send_role(int link)
{
    char buf[40];
    int n = snprintf(buf, sizeof(buf), "{\"t\":\"role\",\"active\":%s}", s.active == link ? "true" : "false");
    send_text(link, buf, (size_t)n);
}

static void send_hello(int link)
{
    char buf[384];
    char id[13];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const char *c6 = s.hooks.c6_version();
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", "hello");
    db_json_int(&j, "proto", DB_PROTO_VERSION);
    db_json_str(&j, "id", id);
    db_json_str(&j, "name", db_config()->name);
    db_json_str(&j, "fw", esp_app_get_description()->version);
    if (c6) {
        db_json_str(&j, "c6", c6);
    } else {
        db_json_null(&j, "c6");
    }
    db_json_str(&j, "board", db_board()->name);
    db_json_bool(&j, "active", s.active == link);
    db_json_obj_close(&j);
    send_text(link, buf, db_json_end(&j));
}

#define STATUS_MAX 4096 /* as /api/status, and the most ws_send takes */

static void send_status(void)
{
    char *buf = malloc(STATUS_MAX);
    size_t n = buf ? s.hooks.status_json(buf, STATUS_MAX) : 0;
    static bool warned;
    if (buf && !n && !warned) {
        warned = true;
        ESP_LOGW(TAG, "status does not fit %d bytes, servers stop getting it", STATUS_MAX);
    }
    send_text(ALL_LINKS, buf, n);
    free(buf);
}

/* Returns true when the active server changed; every connected server then got a role message. */
static bool recompute_active(void)
{
    xSemaphoreTake(s.role_lock, portMAX_DELAY); /* every link task calls this; hooks must see changes in order */
    /* the chosen server when it is up, else the first one that is */
    int next = -1;
    const char *host = db_config()->host;
    for (int i = 0; i < DB_MAX_SERVERS && s.enabled; i++) {
        if (!s.links[i].connected) {
            continue;
        }
        next = next < 0 ? i : next;
        if (strcmp(s.links[i].url, host) == 0) {
            next = i;
            break;
        }
    }
    if (next == s.active) {
        xSemaphoreGive(s.role_lock);
        return false;
    }
    ESP_LOGI(TAG, "active server %d -> %d", s.active, next);
    s.active = next;
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (s.links[i].connected) {
            send_role(i);
        }
    }
    s.hooks.active_changed(next);
    xSemaphoreGive(s.role_lock);
    return true;
}

static void handle_command(link_t *l, const char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(t)) {
        ESP_LOGW(TAG, "server %d: message without type", l->index);
    } else if (strcmp(t->valuestring, "brightness") == 0) {
        const cJSON *pct = cJSON_GetObjectItemCaseSensitive(root, "pct");
        if (cJSON_IsNumber(pct) && l->index == s.active) {
            int v = pct->valueint;
            db_deck_set_brightness((uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v)));
        }
    } else if (strcmp(t->valuestring, "ota") == 0) {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(root, "target");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
        if (cJSON_IsString(target) && cJSON_IsString(url) && l->index == s.active) {
            s.hooks.ota_requested(target->valuestring, url->valuestring);
        }
    } else if (strcmp(t->valuestring, "reboot") == 0 && l->index != s.active) {
        ESP_LOGW(TAG, "server %d asked for a restart, but only the active server may", l->index);
    } else if (strcmp(t->valuestring, "reboot") == 0) {
        ESP_LOGW(TAG, "server %d asked for a restart", l->index);
        db_ota_restart(200);
    } else {
        ESP_LOGW(TAG, "server %d: unknown message %s", l->index, t->valuestring);
    }
    cJSON_Delete(root);
}

/* The image goes straight from the socket into one PSRAM buffer that the deck takes over. */
static int read_image(link_t *l, uint64_t len)
{
    uint8_t hdr[DB_IMAGE_HDR_LEN];
    if (len <= sizeof(hdr)) {
        return ws_skip(&l->ws, len - 1); /* too short to carry a sequence number we could acknowledge */
    }
    hdr[0] = DB_MSG_IMAGE;
    int err = ws_read(&l->ws, hdr + 1, sizeof(hdr) - 1);
    if (err) {
        return err;
    }
    db_image_hdr_t h;
    uint64_t rest = len - sizeof(hdr);
    size_t n = (size_t)rest; /* used only when len <= IMAGE_MAX */
    db_ack_result_t refuse = DB_ACK_SHOWN;
    uint8_t *img = NULL;
    if (db_proto_parse_image_hdr(hdr, sizeof(hdr), &h) != DB_OK) {
        h.display = hdr[1];
        h.seq = db_get_u32(hdr + 10);
        refuse = DB_ACK_REJECTED_RECT;
    } else if (len > IMAGE_MAX || !(img = heap_caps_malloc(n, MALLOC_CAP_SPIRAM))) {
        refuse = DB_ACK_BUSY;
    }
    if (refuse != DB_ACK_SHOWN) {
        /* The server waits for this ack before it sends to the area again. */
        uint8_t out[DB_ACK_LEN];
        db_proto_write_ack(out, refuse, h.display, h.seq, 0, 0, 0);
        send_inline(l->index, WS_OP_BINARY, out, sizeof(out), 0);
        return ws_skip(&l->ws, rest);
    }
    err = ws_read(&l->ws, img, n);
    if (err) {
        heap_caps_free(img);
        return err;
    }
    db_deck_submit((db_source_t)(DB_SOURCE_LINK0 + l->index), h.display, h.rect, h.seq, img, n,
                   (uint64_t)esp_timer_get_time());
    return 0;
}

static int read_binary(link_t *l, uint64_t len)
{
    if (len == 0) {
        return 0;
    }
    uint8_t type;
    int err = ws_read(&l->ws, &type, 1);
    if (err) {
        return err;
    }
    if (type == DB_MSG_IMAGE) {
        return read_image(l, len);
    }
    if (type == DB_MSG_TIME && len == DB_TIME_LEN) {
        uint8_t msg[DB_TIME_LEN] = {type};
        uint64_t server;
        err = ws_read(&l->ws, msg + 1, sizeof(msg) - 1);
        if (!err && db_proto_parse_time(msg, sizeof(msg), &server) == DB_OK) {
            uint8_t out[DB_TIME_REPLY_LEN];
            db_proto_write_time_reply(out, server, (uint64_t)esp_timer_get_time());
            send_inline(l->index, WS_OP_BINARY, out, sizeof(out), 0);
        }
        return err;
    }
    return ws_skip(&l->ws, len - 1);
}

static int read_text(link_t *l, uint64_t len)
{
    if (len >= TEXT_MAX) {
        return ws_skip(&l->ws, len);
    }
    int err = ws_read(&l->ws, l->text, (size_t)len);
    if (!err) {
        l->text[len] = '\0';
        handle_command(l, l->text);
    }
    return err;
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
        return read_binary(l, len);
    case WS_OP_TEXT:
        return read_text(l, len);
    case WS_OP_PING: {
        uint8_t payload[125];
        if (len > sizeof(payload)) {
            return -EPROTO;
        }
        err = ws_read(&l->ws, payload, (size_t)len);
        return err ? err : ws_send(&l->ws, WS_OP_PONG, payload, (size_t)len);
    }
    case WS_OP_CLOSE:
        ws_skip(&l->ws, len);
        ws_send(&l->ws, WS_OP_CLOSE, NULL, 0);
        return -ECONNRESET;
    default:
        return ws_skip(&l->ws, len);
    }
}

/* true when the server went silent: the path may be dead, not only the server */
static bool session(link_t *l)
{
    l->last_rx_ms = l->last_ping_ms = now_ms();
    for (;;) {
        int err = read_frame(l);
        int64_t now = now_ms();
        if (l->reconnect) {
            ESP_LOGI(TAG, "server %d: %s", l->index, (const char *)l->reconnect);
            l->reconnect = NULL;
            return false;
        }
        if (err == -EAGAIN || err == 0) {
            if (now - l->last_rx_ms > SILENT_LIMIT_MS) {
                ESP_LOGW(TAG, "server %d silent for %d s", l->index, SILENT_LIMIT_MS / 1000);
                return true;
            }
            if (now - l->last_ping_ms > PING_AFTER_MS) {
                l->last_ping_ms = now;
                ws_send(&l->ws, WS_OP_PING, NULL, 0);
            }
            continue;
        }
        db_log_event(TAG, "source", "server %d lost: %s", l->index, strerror(-err));
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
    int logged_err[2] = {0}; /* per LAN/WLAN, which alternate: a server that stays away logs once per error */
    uint32_t wait_ms = RETRY_MS;
    char failed[sizeof(l->url)] = "";
    for (;;) {
        if (!l->url[0]) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
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
        /* the token only goes to a server the user set, never to one that announced itself over mDNS */
        bool set = strcmp(url, db_config_server_of(db_config(), l->index)) == 0;
        uint8_t pin[32];
        bool pinned = set && db_config_pin(db_config()->server_pins[l->index], pin);
        int err = ws_connect(&l->ws, url, "deckconnector.v1", s.headers[0] && set ? s.headers : NULL,
                             bound ? ifname : NULL, CONNECT_TIMEOUT, pinned ? pin : NULL);
        if (err) {
            db_health_fail(&l->health, strerror(-err));
            if (err != logged_err[wlan]) {
                logged_err[wlan] = err;
                ESP_LOGW(TAG, "server %d over %s: %s", l->index, over, strerror(-err));
            }
            /* A refused connection proves the path works and only the server is missing, so the next try goes
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
        wait_ms = RETRY_MS;
        failed[0] = '\0';
        via_wlan = false; /* next time start with LAN again */
        logged_err[0] = logged_err[1] = 0;
        db_log_event(TAG, "source", "server %d connected over %s: %s", l->index, over, l->url);
        l->over_wlan = bound && wlan;
        db_health_ok(&l->health, l->over_wlan);
        l->connected = true;
        db_log_mark(DB_MARK_SERVER);
        if (!recompute_active()) {
            send_role(l->index);
        }
        send_hello(l->index);
        send_device(l->index);
        /* silent over LAN: a LAN that keeps its link but drops everything would cost a connect timeout */
        via_wlan = session(l) && !l->over_wlan;
        l->connected = false;
        db_health_fail(&l->health, "connection lost");
        ws_close(&l->ws);
        recompute_active();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void on_ack(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv, uint64_t t_start,
                   uint64_t t_done, void *arg)
{
    uint8_t out[DB_ACK_LEN];
    db_proto_write_ack(out, result, display, seq, t_recv, t_start, t_done);
    send_inline((int)(intptr_t)arg, WS_OP_BINARY, out, sizeof(out), 0);
}

static void on_input(const db_input_t *ev)
{
    int a = s.active;
    if (a >= 0 && db_deck_writer() == (db_source_t)(DB_SOURCE_LINK0 + a)) {
        uint8_t out[DB_INPUT_LEN];
        db_proto_write_input(out, ev);
        if (!send_inline(a, WS_OP_BINARY, out, sizeof(out), pdMS_TO_TICKS(20))) {
            s.inputs_dropped++;
        }
    }
}

static void on_device(const db_device_info_t *info)
{
    send_device(ALL_LINKS);
}

static bool any_configured(void)
{
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (db_config_server_of(db_config(), i)[0]) {
            return true;
        }
    }
    return false;
}

/* Without configured servers, use the first ones that announce _deckconnector._tcp. */
static void discover(void)
{
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_deckconnector", "_tcp", 2000, DB_MAX_SERVERS, &results) != ESP_OK) {
        return;
    }
    int i = 0;
    for (mdns_result_t *r = results; r && i < DB_MAX_SERVERS; r = r->next, i++) {
        link_t *l = &s.links[i];
        if (r->addr && !l->connected) {
            snprintf(l->url, sizeof(l->url), "ws://" IPSTR ":%u/deckbridge",
                     IP2STR(&r->addr->addr.u_addr.ip4), r->port);
            ESP_LOGI(TAG, "found server %s (%s)", l->url, r->instance_name ? r->instance_name : "?");
        }
    }
    mdns_query_results_free(results);
}

/* A link on WLAN goes back to LAN as soon as the server answers there again: after a start, when WLAN was up
 * first, or when a dead switch port works again. The probe is one TCP connect, so a dead LAN never wins. */
static void back_to_lan(void)
{
    char lan[6];
    if (!db_net_ifname(false, lan)) {
        return;
    }
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        link_t *l = &s.links[i];
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
    int64_t next_status = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DISCOVER_EVERY));
        if (!any_configured() && s.active < 0 && s.enabled) { /* only while a server is to draw */
            discover();
        }
        if (now_ms() >= next_status) {
            next_status = now_ms() + STATUS_PERIOD;
            send_status();
        }
        back_to_lan();
    }
}

esp_err_t db_link_start(const db_link_hooks_t *hooks)
{
    s.hooks = *hooks;
    s.tx = xQueueCreate(TX_QUEUE, sizeof(tx_t));
    s.role_lock = xSemaphoreCreateMutex();
    if (!s.tx || !s.role_lock) {
        return ESP_ERR_NO_MEM;
    }
    const db_config_t *c = db_config();
    if (c->token[0]) {
        snprintf(s.headers, sizeof(s.headers), "Authorization: Bearer %s\r\n", c->token);
    }
    db_deck_listener_t dl = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&dl);
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        link_t *l = &s.links[i];
        l->index = i;
        if (ws_init(&l->ws)) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(l->url, db_config_server_of(c, i), sizeof(l->url));
        db_deck_set_ack_handler((db_source_t)(DB_SOURCE_LINK0 + i), on_ack, (void *)(intptr_t)i);
        char name[12];
        snprintf(name, sizeof(name), "link%d", i);
        if (xTaskCreatePinnedToCore(link_task, name, 6144, l, 6, &l->task, 0) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    db_link_reload(); /* takes note of the pins, before any link is up */
    bool ok = xTaskCreatePinnedToCore(tx_task, "link_tx", 4096, NULL, 7, NULL, 0) == pdPASS &&
              xTaskCreatePinnedToCore(supervisor_task, "link_sup", 4096, NULL, 3, NULL, 0) == pdPASS;
    return ok ? ESP_OK : ESP_ERR_NO_MEM;
}

void db_link_reload(void)
{
    /* only what the config changed: a server found by mDNS keeps its URL while the config stays empty */
    static char applied[DB_MAX_SERVERS][sizeof(s.links[0].url)];
    static char applied_pin[DB_MAX_SERVERS][sizeof(db_config()->server_pins[0])];
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        const char *want = db_config_server_of(db_config(), i);
        const char *pin = db_config()->server_pins[i];
        if (strcmp(pin, applied_pin[i]) != 0) {
            strlcpy(applied_pin[i], pin, sizeof(applied_pin[i]));
            if (s.links[i].connected) { /* the next connect checks the new one anyway */
                s.links[i].reconnect = "certificate pin changed";
            }
        }
        if (strcmp(want, applied[i]) == 0) {
            continue;
        }
        strlcpy(applied[i], want, sizeof(applied[i]));
        if (strcmp(want, s.links[i].url) != 0) {
            strlcpy(s.links[i].url, want, sizeof(s.links[i].url));
            s.links[i].reconnect = "address changed";
            db_link_retry(i);
        }
    }
}

void db_link_set_enabled(bool enabled)
{
    s.enabled = enabled;
    recompute_active();
}

void db_link_resend_role(void)
{
    if (s.active >= 0) {
        send_role(s.active);
    }
}

uint32_t db_link_inputs_dropped(void)
{
    return s.inputs_dropped;
}

bool db_link_connected(int index)
{
    return index >= 0 && index < DB_MAX_SERVERS && s.links[index].connected;
}

db_health_t db_link_health(int index)
{
    return s.links[index].health;
}

void db_link_retry(int index)
{
    if (s.links[index].task) {
        xTaskNotifyGive(s.links[index].task);
    }
}

const char *db_link_url(int index)
{
    return s.links[index].url;
}
