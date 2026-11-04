#include "db_link.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_board.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_proto.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"

#define TX_INLINE      32
#define TX_QUEUE       64
#define TEXT_MAX       2048
#define IMAGE_MAX      (1024 * 1024)
#define STATUS_PERIOD  10000
#define DISCOVER_EVERY 5000
#define WS_BUFFER      16384
#define OP_TEXT        0x1
#define OP_BINARY      0x2
#define ALL_LINKS      (-1)

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
    esp_websocket_client_handle_t ws;
    char url[96];
    volatile bool connected;
    uint8_t rx_op;
    size_t rx_len;
    size_t rx_got;
    uint64_t rx_start;
    uint8_t hdr[DB_TIME_LEN];
    uint8_t *img;
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
    tx_t m = {.link = (int8_t)link, .op = OP_TEXT, .len = (uint16_t)len, .heap = malloc(len)};
    if (!m.heap) {
        return;
    }
    memcpy(m.heap, json, len);
    if (xQueueSend(s.tx, &m, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(m.heap);
        s.tx_dropped++;
    }
}

static void tx_one(link_t *l, const tx_t *m)
{
    const char *p = m->heap ? m->heap : (const char *)m->data;
    if (!l->connected) {
        return;
    }
    if (m->op == OP_TEXT) {
        esp_websocket_client_send_text(l->ws, p, m->len, pdMS_TO_TICKS(2000));
    } else {
        esp_websocket_client_send_bin(l->ws, p, m->len, pdMS_TO_TICKS(2000));
    }
}

static void tx_task(void *arg)
{
    tx_t m;
    for (;;) {
        xQueueReceive(s.tx, &m, portMAX_DELAY);
        for (int i = 0; i < DB_MAX_SERVERS; i++) {
            if (m.link == ALL_LINKS || m.link == i) {
                tx_one(&s.links[i], &m);
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
    static db_device_info_t info; /* only used from the deck event task and link tasks, never at once */
    char *buf = malloc(4096);
    if (!buf) {
        return;
    }
    bool present = db_deck_info(&info);
    send_text(link, buf, db_proto_device_json(present ? &info : NULL, buf, 4096));
    free(buf);
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
    if (!buf || !s.hooks.status_json) {
        free(buf);
        return;
    }
    send_text(ALL_LINKS, buf, s.hooks.status_json(buf, 1024));
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

static void finish_binary(link_t *l)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (l->hdr[0] == DB_MSG_IMAGE && l->img) {
        db_image_hdr_t h;
        if (db_proto_parse_image_hdr(l->hdr, DB_IMAGE_HDR_LEN, &h) == DB_OK) {
            db_deck_submit((db_source_t)(DB_SOURCE_LINK0 + l->index), h.display, h.rect, h.seq, l->img,
                           l->rx_len - DB_IMAGE_HDR_LEN, now);
        } else {
            heap_caps_free(l->img);
        }
        l->img = NULL;
    } else if (l->hdr[0] == DB_MSG_TIME) {
        uint64_t server;
        if (db_proto_parse_time(l->hdr, DB_TIME_LEN, &server) == DB_OK) {
            uint8_t out[DB_TIME_REPLY_LEN];
            db_proto_write_time_reply(out, server, now);
            send_inline(l->index, OP_BINARY, out, sizeof(out));
        }
    }
}

/* The header goes to `hdr`; image bytes go straight into one PSRAM buffer, so they are copied only once. */
static void feed_binary(link_t *l, const uint8_t *p, size_t n)
{
    while (n > 0) {
        uint8_t type = l->rx_got > 0 ? l->hdr[0] : p[0];
        size_t hdr_len = type == DB_MSG_IMAGE ? DB_IMAGE_HDR_LEN : sizeof(l->hdr);
        if (l->rx_got < hdr_len) {
            size_t take = hdr_len - l->rx_got < n ? hdr_len - l->rx_got : n;
            memcpy(l->hdr + l->rx_got, p, take);
            l->rx_got += take;
            p += take;
            n -= take;
            if (type == DB_MSG_IMAGE && l->rx_got == DB_IMAGE_HDR_LEN && l->rx_len > DB_IMAGE_HDR_LEN) {
                l->img = heap_caps_malloc(l->rx_len - DB_IMAGE_HDR_LEN, MALLOC_CAP_SPIRAM);
            }
            continue;
        }
        if (l->img) {
            memcpy(l->img + (l->rx_got - DB_IMAGE_HDR_LEN), p, n);
        }
        l->rx_got += n;
        n = 0;
    }
}

static void on_data(link_t *l, const esp_websocket_event_data_t *e)
{
    if ((e->op_code == OP_TEXT || e->op_code == OP_BINARY) && e->payload_offset == 0) {
        heap_caps_free(l->img);
        l->img = NULL;
        l->rx_op = e->op_code;
        l->rx_len = (size_t)e->payload_len;
        l->rx_got = 0;
        memset(l->hdr, 0, sizeof(l->hdr));
    } else if (e->op_code != 0 && e->op_code != OP_TEXT && e->op_code != OP_BINARY) {
        return; /* ping, pong, close */
    }
    if (l->rx_len > IMAGE_MAX) {
        return;
    }
    if (l->rx_op == OP_BINARY) {
        feed_binary(l, (const uint8_t *)e->data_ptr, (size_t)e->data_len);
        if (l->rx_got >= l->rx_len) {
            finish_binary(l);
        }
    } else if (l->rx_op == OP_TEXT && l->rx_len < TEXT_MAX) {
        memcpy(l->text + e->payload_offset, e->data_ptr, (size_t)e->data_len);
        if ((size_t)(e->payload_offset + e->data_len) >= l->rx_len) {
            l->text[l->rx_len] = '\0';
            handle_command(l, l->text);
        }
    }
}

static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    link_t *l = arg;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "server %d connected: %s", l->index, l->url);
        l->connected = true;
        if (!recompute_active()) {
            send_role(l->index);
        }
        send_hello(l->index);
        send_device(l->index);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        if (l->connected) {
            ESP_LOGW(TAG, "server %d lost", l->index);
        }
        l->connected = false;
        heap_caps_free(l->img);
        l->img = NULL;
        recompute_active();
        break;
    case WEBSOCKET_EVENT_DATA:
        on_data(l, data);
        break;
    default:
        break;
    }
}

/* ---------- deck events ---------- */

static void on_ack(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv, uint64_t t_start,
                   uint64_t t_done, void *arg)
{
    uint8_t out[DB_ACK_LEN];
    db_proto_write_ack(out, result, display, seq, t_recv, t_start, t_done);
    send_inline((int)(intptr_t)arg, OP_BINARY, out, sizeof(out));
}

static void on_input(const db_input_t *ev, void *arg)
{
    int a = s.active;
    if (a >= 0 && db_deck_writer() == (db_source_t)(DB_SOURCE_LINK0 + a)) {
        uint8_t out[DB_INPUT_LEN];
        db_proto_write_input(out, ev);
        send_inline(a, OP_BINARY, out, sizeof(out));
    }
}

static void on_device(const db_device_info_t *info, void *arg)
{
    send_device(ALL_LINKS);
}

/* ---------- lifecycle ---------- */

static void start_client(link_t *l)
{
    esp_websocket_client_config_t cfg = {
        .uri = l->url,
        .subprotocol = "deckbridge.v1",
        .headers = s.headers[0] ? s.headers : NULL,
        .buffer_size = WS_BUFFER,
        .task_stack = 6144,
        .task_prio = 6,
        .ping_interval_sec = 5,
        .pingpong_timeout_sec = 15,
        .reconnect_timeout_ms = 2000,
        .network_timeout_ms = 5000,
        .user_context = l,
    };
    l->ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(l->ws, WEBSOCKET_EVENT_ANY, on_ws_event, l);
    esp_websocket_client_start(l->ws);
}

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
    for (mdns_result_t *r = results; r && i < DB_MAX_SERVERS; r = r->next) {
        link_t *l = &s.links[i];
        if (!r->addr || l->ws) {
            i++;
            continue;
        }
        snprintf(l->url, sizeof(l->url), "ws://" IPSTR ":%u/deckbridge", IP2STR(&r->addr->addr.u_addr.ip4),
                 r->port);
        ESP_LOGI(TAG, "found server %s (%s)", l->url, r->instance_name ? r->instance_name : "?");
        start_client(l);
        i++;
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
        if (esp_timer_get_time() / 1000 >= next_status) {
            next_status = esp_timer_get_time() / 1000 + STATUS_PERIOD;
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
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        link_t *l = &s.links[i];
        l->index = i;
        db_deck_set_ack_handler((db_source_t)(DB_SOURCE_LINK0 + i), on_ack, (void *)(intptr_t)i);
        if (c->servers[i][0]) {
            strlcpy(l->url, c->servers[i], sizeof(l->url));
            start_client(l);
        }
    }
    db_deck_listener_t dl = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&dl);
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
