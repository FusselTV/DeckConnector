#include "db_dock.h"

#include <stdio.h>
#include <string.h>

#include "db_cora.h"
#include "db_deck.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"

#define PAYLOAD_MAX  8192
#define KEEPALIVE_MS 1500
#define FEATURE_LEN  64
#define IDLE_TIMEOUT 10 /* s without data from the host */

static const char *TAG = "dock";

/* One host connection on one port. The dock port answers about the dock, the device port passes HID through.
 */
typedef struct {
    const char *name;
    uint16_t port;
    bool device_port;
    uint8_t connection_no;
    volatile int sock;
    SemaphoreHandle_t tx;
} conn_t;

static struct {
    void (*client_changed)(bool connected);
    conn_t dock;
    conn_t device;
    db_cora_dock_t info;
    db_device_info_t attached;
    SemaphoreHandle_t info_lock;
} s;

static void send_frame(conn_t *c, uint16_t flags, uint8_t op, uint32_t id, const uint8_t *payload, size_t len)
{
    xSemaphoreTake(c->tx, portMAX_DELAY);
    int sock = c->sock;
    if (sock >= 0) {
        uint8_t hdr[DB_CORA_HDR_LEN];
        db_cora_write_hdr(hdr, &(db_cora_hdr_t){.flags = flags, .op = op, .id = id, .len = (uint32_t)len});
        if (send(sock, hdr, sizeof(hdr), 0) < 0 || send(sock, payload, len, 0) < 0) {
            shutdown(sock, SHUT_RDWR);
        }
    }
    xSemaphoreGive(c->tx);
}

static void send_dock_report(conn_t *c, uint8_t cmd, uint32_t id)
{
    uint8_t out[DB_CORA_REPORT_LEN];
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    db_cora_dock_report(cmd, &s.info, out);
    xSemaphoreGive(s.info_lock);
    send_frame(c, 0, 0, id, out, sizeof(out));
}

static void handle_dock(const db_cora_hdr_t *h, const uint8_t *p)
{
    if (h->op == DB_CORA_OP_GET_REPORT && h->len >= 2 && p[0] == 0x03) {
        send_dock_report(&s.dock, p[1], h->id);
    }
    /* Keepalive acks (03 1a ..) and dock settings need no answer. */
}

static void handle_device(const db_cora_hdr_t *h, const uint8_t *p)
{
    if (!(h->flags & DB_CORA_FLAG_VERBATIM)) {
        /* Hosts ask the device port which device it carries. 0x80 stays unanswered on purpose: an answer
         * would make the host treat this port as a dock. */
        if (h->op == DB_CORA_OP_GET_REPORT && h->len >= 2 && p[0] == 0x03 && p[1] == 0x1c) {
            send_dock_report(&s.device, 0x1c, h->id);
        }
        return;
    }
    switch (h->op) {
    case DB_CORA_OP_WRITE:
        db_deck_raw_write(p, h->len);
        break;
    case DB_CORA_OP_SEND_REPORT:
        db_deck_raw_set_feature(p, h->len);
        break;
    case DB_CORA_OP_GET_REPORT: {
        uint8_t buf[FEATURE_LEN] = {h->len ? p[0] : 0};
        size_t n = 0;
        if (db_deck_raw_get_feature(buf, sizeof(buf), &n) != ESP_OK) {
            ESP_LOGW(TAG, "feature report 0x%02x failed", buf[0]);
            n = 1; /* answer anyway with just the id, so the host does not wait for its timeout */
        }
        send_frame(&s.device, DB_CORA_FLAG_VERBATIM | DB_CORA_FLAG_RESULT, DB_CORA_OP_GET_REPORT, h->id, buf,
                   n);
        break;
    }
    default:
        break;
    }
}

static bool read_full(int sock, uint8_t *buf, size_t len)
{
    while (len > 0) {
        int n = recv(sock, buf, len, 0);
        if (n <= 0) {
            return false;
        }
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

static void serve(conn_t *c, int sock, uint8_t *payload)
{
    struct timeval tv = {.tv_sec = IDLE_TIMEOUT};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    /* A host that stops reading must not block the deck's input path, which sends from its own task. */
    struct timeval send_tv = {.tv_sec = 2};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    c->sock = sock;
    if (c->device_port && s.client_changed) {
        s.client_changed(true);
    }
    uint8_t hdr_buf[DB_CORA_HDR_LEN];
    db_cora_hdr_t h;
    while (read_full(sock, hdr_buf, sizeof(hdr_buf)) &&
           db_cora_parse_hdr(hdr_buf, sizeof(hdr_buf), &h) == DB_OK && h.len <= PAYLOAD_MAX &&
           read_full(sock, payload, h.len)) {
        if (c->device_port) {
            handle_device(&h, payload);
        } else {
            handle_dock(&h, payload);
        }
    }
    xSemaphoreTake(c->tx, portMAX_DELAY);
    c->sock = -1;
    xSemaphoreGive(c->tx);
    close(sock);
    ESP_LOGI(TAG, "%s host left", c->name);
    if (c->device_port && s.client_changed) {
        s.client_changed(false);
    }
}

static void listen_task(void *arg)
{
    conn_t *c = arg;
    uint8_t *payload = heap_caps_malloc(PAYLOAD_MAX, MALLOC_CAP_INTERNAL);
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(c->port)};
    if (!payload || bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        ESP_LOGE(TAG, "cannot listen on %u", c->port);
        vTaskDelete(NULL);
    }
    for (;;) {
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int sock = accept(ls, (struct sockaddr *)&from, &len);
        if (sock < 0) {
            continue;
        }
        ESP_LOGI(TAG, "%s host %s", c->name, inet_ntoa(from.sin_addr));
        serve(c, sock, payload);
    }
}

static void keepalive_task(void *arg)
{
    uint8_t buf[DB_CORA_REPORT_LEN];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_MS));
        conn_t *conns[] = {&s.dock, &s.device};
        for (int i = 0; i < 2; i++) {
            if (conns[i]->sock >= 0) {
                db_cora_keepalive(conns[i]->connection_no, buf);
                send_frame(conns[i], 0, 0, 0, buf, sizeof(buf));
            }
        }
    }
}

static void on_raw_input(const uint8_t *report, size_t len)
{
    if (s.device.sock < 0) {
        return;
    }
    uint8_t buf[DB_CORA_REPORT_LEN] = {0}; /* the dock forwards full 512 byte input reports */
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    send_frame(&s.device, DB_CORA_FLAG_VERBATIM, 0, 0, buf, sizeof(buf));
}

/* Tell the host about plug events; it then opens or closes the device port. */
static void on_device(const db_device_info_t *info)
{
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    if (info) {
        s.attached = *info;
    }
    s.info.device = info ? &s.attached : NULL;
    xSemaphoreGive(s.info_lock);
    if (s.dock.sock >= 0) {
        send_dock_report(&s.dock, 0x1c, 0);
    }
}

static void announce(void)
{
    char instance[48];
    snprintf(instance, sizeof(instance), "Stream Deck Network Dock %02X%02X%02X", s.info.mac[3],
             s.info.mac[4], s.info.mac[5]);
    char id[18];
    snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X", s.info.mac[0], s.info.mac[1], s.info.mac[2],
             s.info.mac[3], s.info.mac[4], s.info.mac[5]);
    mdns_txt_item_t txt[] = {
        {"dt", "215"},
        {"mf", "Elgato"},
        {"md", "Stream Deck Network Dock 20GBW9901"},
        {"pv", "1.0"},
        {"id", id},
        {"sn", s.info.serial},
        {"fw", DB_CORA_FIRMWARE},
        {"hwrev", "0200"},
    };
    esp_err_t err =
        mdns_service_add(instance, "_elg", "_tcp", DB_CORA_DOCK_PORT, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS announcement failed: %s", esp_err_to_name(err));
    }
}

esp_err_t db_dock_start(void (*client_changed)(bool connected))
{
    s.client_changed = client_changed;
    s.info_lock = xSemaphoreCreateMutex();
    esp_read_mac(s.info.mac, ESP_MAC_BASE);
    snprintf(s.info.serial, sizeof(s.info.serial), "DB%02X%02X%02X%02X%02X%02X", s.info.mac[0], s.info.mac[1],
             s.info.mac[2], s.info.mac[3], s.info.mac[4], s.info.mac[5]);
    s.dock = (conn_t){.name = "dock",
                      .port = DB_CORA_DOCK_PORT,
                      .connection_no = 1,
                      .sock = -1,
                      .tx = xSemaphoreCreateMutex()};
    s.device = (conn_t){.name = "device",
                        .port = DB_CORA_DEVICE_PORT,
                        .device_port = true,
                        .connection_no = 2,
                        .sock = -1,
                        .tx = xSemaphoreCreateMutex()};
    db_deck_listener_t l = {.device_changed = on_device, .raw_input = on_raw_input};
    db_deck_add_listener(&l);
    db_device_info_t now;
    if (db_deck_info(&now)) {
        on_device(&now);
    }
    xTaskCreatePinnedToCore(listen_task, "dock_port", 4096, &s.dock, 6, NULL, 0);
    xTaskCreatePinnedToCore(listen_task, "dock_dev", 4096, &s.device, 7, NULL, 0);
    xTaskCreatePinnedToCore(keepalive_task, "dock_ka", 3072, NULL, 5, NULL, 0);
    announce();
    ESP_LOGI(TAG, "Network Dock emulation on ports %d and %d", DB_CORA_DOCK_PORT, DB_CORA_DEVICE_PORT);
    return ESP_OK;
}
