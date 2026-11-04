#include "db_link.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_board.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_net.h"
#include "db_proto.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"
#include "ws.h"

#define TX_INLINE       32
#define TX_QUEUE        64
#define TEXT_MAX        2048
#define IMAGE_MAX       (1024 * 1024)
#define STATUS_PERIOD   10000
#define DISCOVER_EVERY  5000
#define RETRY_MS        2000
#define CONNECT_TIMEOUT 3000
#define PING_AFTER_MS   5000
#define SILENT_LIMIT_MS 10000
#define ALL_LINKS       (-1)

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
    int64_t last_rx_ms;
    int64_t last_ping_ms;
    char text[TEXT_MAX];
} link_t;

static struct {
    db_link_hooks_t hooks;
    link_t links[DB_MAX_SERVERS];
    volatile int active;
    QueueHandle_t tx;
    char headers[96];
    uint32_t tx_dropped;
} s = {.active = -1};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------- sending ---------- */

static void send_inline(int link, uint8_t op, const void *data, size_t len)
{
    tx_t m = {.link = (int8_t)link, .op = op, .len = (uint16_t)len};
    memcpy(m.data, data, len);
    if (xQueueSend(s.tx, &m, 0) != pdTRUE) {
        s.tx_dropped++;
    }
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
        s.tx_dropped++;
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
            if ((m.link == ALL_LINKS || m.link == i) && l->connected) {
                ws_send(&l->ws, m.op, m.heap ? (const void *)m.heap : m.data, m.len);
            }
        }
        free(m.heap);
    }
}

void db_link_broadcast(const char *json)
{
    send_text(ALL_LINKS, json, strlen(json));
}

/* ---------- messages to the server ---------- */

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
    const char *c6 = s.hooks.c6_version ? s.hooks.c6_version() : NULL;
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

static void send_status(void)
{
    char *buf = malloc(1024);
    if (buf && s.hooks.status_json) {
        send_text(ALL_LINKS, buf, s.hooks.status_json(buf, 1024));
    }
    free(buf);
}

/* ---------- roles ---------- */

/* Returns true when the active server changed; every connected server then got a role message. */
static bool recompute_active(void)
{
    int next = -1;
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (s.links[i].connected) {
            next = i;
            break;
        }
    }
    if (next == s.active) {
        return false;
    }
    ESP_LOGI(TAG, "active server %d -> %d", s.active, next);
    s.active = next;
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (s.links[i].connected) {
            send_role(i);
        }
    }
    if (s.hooks.active_changed) {
        s.hooks.active_changed(next);
    }
    return true;
}

/* ---------- receiving ---------- */

static void handle_command(link_t *l, const char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(t)) {
        ESP_LOGW(TAG, "server %d: message without type", l->index);
    } else if (strcmp(t->valuestring, "brightness") == 0) {
        const cJSON *pct = cJSON_GetObjectItemCaseSensitive(root, "pct");
        if (cJSON_IsNumber(pct) && l->index == s.active) {
            db_deck_set_brightness((uint8_t)(pct->valueint < 0 ? 0 : pct->valueint));
        }
    } else if (strcmp(t->valuestring, "ota") == 0) {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(root, "target");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
        if (cJSON_IsString(target) && cJSON_IsString(url) && s.hooks.ota_requested) {
            s.hooks.ota_requested(target->valuestring, url->valuestring);
        }
    } else if (strcmp(t->valuestring, "reboot") == 0) {
        ESP_LOGW(TAG, "server %d asked for a restart", l->index);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else {
        ESP_LOGW(TAG, "server %d: unknown message %s", l->index, t->valuestring);
    }
    cJSON_Delete(root);
}

/* The image goes straight from the socket into one PSRAM buffer that the deck takes over. */
static int read_image(link_t *l, uint64_t len)
{
    uint8_t hdr[DB_IMAGE_HDR_LEN];
    hdr[0] = DB_MSG_IMAGE;
    int err = ws_read(&l->ws, hdr + 1, sizeof(hdr) - 1);
    db_image_hdr_t h;
    if (err || len <= sizeof(hdr) || len > IMAGE_MAX ||
        db_proto_parse_image_hdr(hdr, sizeof(hdr), &h) != DB_OK) {
        return err ? err : ws_skip(&l->ws, len - sizeof(hdr));
    }
    size_t n = (size_t)len - sizeof(hdr);
    uint8_t *img = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!img) {
        return ws_skip(&l->ws, n);
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
            send_inline(l->index, WS_OP_BINARY, out, sizeof(out));
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

static void session(link_t *l)
{
    l->last_rx_ms = l->last_ping_ms = now_ms();
    for (;;) {
        int err = read_frame(l);
        int64_t now = now_ms();
        if (err == -EAGAIN || err == 0) {
            if (now - l->last_rx_ms > SILENT_LIMIT_MS) {
                ESP_LOGW(TAG, "server %d silent for %d s", l->index, SILENT_LIMIT_MS / 1000);
                return;
            }
            if (now - l->last_ping_ms > PING_AFTER_MS) {
                l->last_ping_ms = now;
                ws_send(&l->ws, WS_OP_PING, NULL, 0);
            }
            continue;
        }
        ESP_LOGW(TAG, "server %d lost: %s", l->index, strerror(-err));
        return;
    }
}

/*
 * A LAN can keep its link and still drop everything (a switch port that stops forwarding). Routing would keep
 * using it, so after a failed attempt the next one goes out over the WLAN interface on purpose, and back.
 */
static void link_task(void *arg)
{
    link_t *l = arg;
    bool via_wlan = false;
    int64_t next_log = 0;
    for (;;) {
        if (!l->url[0]) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
            continue;
        }
        char ifname[6];
        bool wlan = via_wlan && db_net_wlan_ifname(ifname);
        int err = ws_connect(&l->ws, l->url, "deckbridge.v1", s.headers[0] ? s.headers : NULL,
                             wlan ? ifname : NULL, CONNECT_TIMEOUT);
        if (err) {
            if (now_ms() >= next_log) {
                next_log = now_ms() + 30000;
                ESP_LOGW(TAG, "server %d over %s: %s", l->index, wlan ? "WLAN" : "the default route",
                         strerror(-err));
            }
            via_wlan = !via_wlan;
            vTaskDelay(pdMS_TO_TICKS(wlan ? RETRY_MS : 200));
            continue;
        }
        ESP_LOGI(TAG, "server %d connected over %s: %s", l->index, wlan ? "WLAN" : "the default route",
                 l->url);
        l->connected = true;
        if (!recompute_active()) {
            send_role(l->index);
        }
        send_hello(l->index);
        send_device(l->index);
        session(l);
        l->connected = false;
        ws_close(&l->ws);
        recompute_active();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ---------- deck events ---------- */

static void on_ack(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv, uint64_t t_start,
                   uint64_t t_done, void *arg)
{
    uint8_t out[DB_ACK_LEN];
    db_proto_write_ack(out, result, display, seq, t_recv, t_start, t_done);
    send_inline((int)(intptr_t)arg, WS_OP_BINARY, out, sizeof(out));
}

static void on_input(const db_input_t *ev, void *arg)
{
    int a = s.active;
    if (a >= 0 && db_deck_writer() == (db_source_t)(DB_SOURCE_LINK0 + a)) {
        uint8_t out[DB_INPUT_LEN];
        db_proto_write_input(out, ev);
        send_inline(a, WS_OP_BINARY, out, sizeof(out));
    }
}

static void on_device(const db_device_info_t *info, void *arg)
{
    send_device(ALL_LINKS);
}

/* ---------- discovery and status ---------- */

static bool any_configured(void)
{
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (db_config()->servers[i][0]) {
            return true;
        }
    }
    return false;
}

/* Without configured servers, use the first ones that announce _deckbridge._tcp. */
static void discover(void)
{
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_deckbridge", "_tcp", 2000, DB_MAX_SERVERS, &results) != ESP_OK || !results) {
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

static void supervisor_task(void *arg)
{
    int64_t next_status = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DISCOVER_EVERY));
        if (!any_configured() && s.active < 0) {
            discover();
        }
        if (now_ms() >= next_status) {
            next_status = now_ms() + STATUS_PERIOD;
            send_status();
        }
    }
}

esp_err_t db_link_start(const db_link_hooks_t *hooks)
{
    s.hooks = *hooks;
    s.tx = xQueueCreate(TX_QUEUE, sizeof(tx_t));
    const db_config_t *c = db_config();
    if (c->token[0]) {
        snprintf(s.headers, sizeof(s.headers), "Authorization: Bearer %s\r\n", c->token);
    }
    db_deck_listener_t dl = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&dl);
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        link_t *l = &s.links[i];
        l->index = i;
        ws_init(&l->ws);
        strlcpy(l->url, c->servers[i], sizeof(l->url));
        db_deck_set_ack_handler((db_source_t)(DB_SOURCE_LINK0 + i), on_ack, (void *)(intptr_t)i);
        char name[12];
        snprintf(name, sizeof(name), "link%d", i);
        xTaskCreatePinnedToCore(link_task, name, 6144, l, 6, NULL, 0);
    }
    xTaskCreatePinnedToCore(tx_task, "link_tx", 4096, NULL, 7, NULL, 0);
    xTaskCreatePinnedToCore(supervisor_task, "link_sup", 4096, NULL, 3, NULL, 0);
    return ESP_OK;
}

int db_link_active(void)
{
    return s.active;
}

bool db_link_connected(int index)
{
    return index >= 0 && index < DB_MAX_SERVERS && s.links[index].connected;
}

const char *db_link_url(int index)
{
    return s.links[index].url;
}
