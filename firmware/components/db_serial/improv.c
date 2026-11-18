#include "db_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "db_config.h"
#include "db_improv.h"
#include "db_json.h"
#include "db_log.h"
#include "db_net.h"
#include "db_web.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_LINUX
#include <poll.h>
#include <unistd.h>
#else
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#define UART CONFIG_ESP_CONSOLE_UART_NUM
#endif

#define OPEN_S   300 /* settings over serial after the start, once setup is done */
#define SCAN_MAX 20

enum { CMD_WIFI = 1, CMD_STATE, CMD_INFO, CMD_SCAN, CMD_HOSTNAME, CMD_NAME, CMD_NETWORK, CMD_SETUP = 0x80 };
enum { READY = 2, PROVISIONING = 3, PROVISIONED = 4 };
enum { E_NONE, E_INVALID, E_UNKNOWN_CMD, E_UNABLE, E_CLOSED, E_BAD_NAME, E_UNKNOWN = 0xff };

static const char *TAG = "improv";
static int64_t started_us;
static int64_t open_us = OPEN_S * 1000000LL;
static bool joining;
static uint8_t last_error;
/* one task: the buffers need no stack */
static uint8_t frame[DB_IMPROV_FRAME_MAX];
static uint8_t data[255];
static char json[256];
static char reply[512];

#if CONFIG_IDF_TARGET_LINUX
static esp_err_t port_open(void)
{
    const char *s = getenv("DB_SIM_IMPROV_OPEN_S");
    open_us = s ? atoll(s) * 1000000LL : open_us;
    return ESP_OK;
}

static int port_read(uint8_t *buf, size_t cap)
{
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
    ssize_t n = poll(&p, 1, 0) > 0 ? read(STDIN_FILENO, buf, cap) : 0;
    if (n <= 0) {
        vTaskDelay(pdMS_TO_TICKS(20)); /* a blocking read would stall the simulated scheduler */
    }
    return n > 0 ? (int)n : 0;
}

static void port_write(const uint8_t *b, size_t n)
{
    if (write(STDOUT_FILENO, b, n) != (ssize_t)n) {
        ESP_LOGW(TAG, "packet cut short");
    }
}
#else
static void log_line(const char *line, size_t len)
{
    char out[2 * 192]; /* every \n becomes \r\n, as on stdout */
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(out) - 1; i++) {
        if (line[i] == '\n') {
            out[n++] = '\r';
        }
        out[n++] = line[i];
    }
    uart_write_bytes(UART, out, n);
}

/* Through the driver, also for stdout: a packet goes out in one uart_write_bytes() call, which log lines
 * written by other tasks cannot split. Plain stdout would also turn each 0x0a byte into CR LF. A log line
 * takes 6 ms on the wire: the TX buffer keeps the task that logs from waiting for it. */
static esp_err_t port_open(void)
{
    esp_err_t err = uart_driver_install(UART, 512, 4096, 0, NULL, 0);
    if (err == ESP_OK) {
        uart_vfs_dev_use_driver(UART);
        db_log_set_sink(log_line);
    }
    return err;
}

static int port_read(uint8_t *buf, size_t cap)
{
    int n = uart_read_bytes(UART, buf, cap, pdMS_TO_TICKS(100));
    return n > 0 ? n : 0;
}

static void port_write(const uint8_t *b, size_t n)
{
    uart_write_bytes(UART, b, n);
}
#endif

static void send(uint8_t type, const uint8_t *d, size_t len)
{
    port_write(frame, db_improv_frame(frame, type, d, (uint8_t)len));
}

static void send_error(uint8_t e)
{
    last_error = e;
    send(DB_IMPROV_ERROR, &e, 1);
}

static void result(uint8_t cmd, const char *const *strs, size_t n)
{
    size_t len = db_improv_result(data, cmd, strs, n);
    if (len) {
        send(DB_IMPROV_RESULT, data, len);
    } else {
        send_error(E_UNKNOWN);
    }
}

static bool lan_ok(const db_net_status_t *n)
{
    return n->lan_up && !n->lan_dead;
}

static void url(const char *ip, char *out, size_t cap)
{
    snprintf(out, cap, "%s://%s", db_config()->https_only ? "https" : "http", ip);
}

/* The state, and the address when there is one: for CMD_STATE and after a WLAN join. */
static void send_state(uint8_t cmd)
{
    db_net_status_t n;
    db_net_status(&n);
    uint8_t st = lan_ok(&n) || n.wlan_up ? PROVISIONED : READY;
    if (n.join_state && strcmp(n.join_state, "testing") == 0) {
        st = PROVISIONING;
    }
    send(DB_IMPROV_STATE, &st, 1);
    if (st == PROVISIONED) {
        char u[32];
        url(lan_ok(&n) ? n.lan_ip : n.wlan_ip, u, sizeof(u));
        const char *s[] = {u};
        result(cmd, s, 1);
    }
}

static void network(void)
{
    db_net_status_t n;
    db_net_status(&n);
    char flags[4], lan[32], wlan[32];
    const char *s[3] = {flags};
    size_t count = 1;
    snprintf(flags, sizeof(flags), "%d", (lan_ok(&n) || n.wlan_up) | n.wlan_up << 1 | lan_ok(&n) << 2);
    if (lan_ok(&n)) {
        url(n.lan_ip, lan, sizeof(lan));
        s[count++] = lan;
    }
    if (n.wlan_up) {
        url(n.wlan_ip, wlan, sizeof(wlan));
        s[count++] = wlan;
    }
    result(CMD_NETWORK, s, count);
}

static void scan(void)
{
    db_net_ap_t *aps = calloc(SCAN_MAX, sizeof(*aps));
    int n = aps ? db_net_wifi_scan(aps, SCAN_MAX) : -1;
    for (int i = 0; i < n; i++) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", aps[i].rssi);
        const char *s[] = {aps[i].ssid, rssi, aps[i].open ? "NO" : "YES"};
        result(CMD_SCAN, s, 3);
    }
    free(aps);
    result(CMD_SCAN, NULL, 0);
}

static void wifi(const uint8_t *d, uint8_t len)
{
    char ssid[33], pass[65];
    size_t pos = 0;
    if (!db_improv_str(d, len, &pos, ssid, sizeof(ssid)) ||
        !db_improv_str(d, len, &pos, pass, sizeof(pass)) || !db_config_wifi_pass_ok(pass)) {
        send_error(E_INVALID);
        return;
    }
    esp_err_t err = db_net_wifi_join(ssid, pass, "");
    if (err != ESP_OK) {
        send_error(err == ESP_ERR_INVALID_ARG ? E_INVALID : E_UNABLE);
        return;
    }
    ESP_LOGI(TAG, "joining WLAN %s", ssid);
    joining = true;
    uint8_t st = PROVISIONING;
    send(DB_IMPROV_STATE, &st, 1);
}

static void join_done(void)
{
    db_net_status_t n;
    db_net_status(&n);
    if (!n.join_state || strcmp(n.join_state, "testing") == 0) {
        return;
    }
    joining = false;
    if (strcmp(n.join_state, "ok") != 0) {
        send_error(E_UNABLE);
    }
    send_state(CMD_WIFI);
}

/* PUT /api/config with what the JSON in `json` holds; its status, `reply` holds the answer. */
static int save(void)
{
    int code = db_web_config(json, reply, sizeof(reply));
    ESP_LOGI(TAG, "settings from serial: %d", code);
    return code;
}

static void name(uint8_t cmd, const uint8_t *d, uint8_t len)
{
    char n[sizeof(db_config()->name)];
    size_t pos = 0;
    if (!db_improv_str(d, len, &pos, n, sizeof(n))) {
        send_error(E_BAD_NAME);
        return;
    }
    db_json_t j;
    db_json_begin(&j, json, sizeof(json));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "name", n);
    db_json_obj_close(&j);
    int code = db_json_end(&j) ? save() : 500;
    if (code != 200) {
        send_error(code == 400 ? E_BAD_NAME : E_UNKNOWN);
        return;
    }
    const char *s[] = {n};
    result(cmd, s, 1);
}

/* Vendor command: admin password, device name, "1" to end setup, and optionally the deck's language; an empty
 * string keeps that setting. */
static void setup(const uint8_t *d, uint8_t len)
{
    char pass[sizeof(db_config()->admin_pass)], n[sizeof(db_config()->name)], done[2],
        lang[sizeof(db_config()->lang)] = "";
    size_t pos = 0;
    if (!db_improv_str(d, len, &pos, pass, sizeof(pass)) || !db_improv_str(d, len, &pos, n, sizeof(n)) ||
        !db_improv_str(d, len, &pos, done, sizeof(done))) {
        const char *s[] = {"error", "admin password, name and done expected"};
        result(CMD_SETUP, s, 2);
        return;
    }
    if (pos < len && !db_improv_str(d, len, &pos, lang, sizeof(lang))) {
        const char *s[] = {"error", "lang: de or en expected"};
        result(CMD_SETUP, s, 2);
        return;
    }
    db_json_t j;
    db_json_begin(&j, json, sizeof(json));
    db_json_obj_open(&j, NULL);
    if (pass[0]) {
        db_json_str(&j, "admin_pass", pass);
    }
    if (n[0]) {
        db_json_str(&j, "name", n);
    }
    if (strcmp(done, "1") == 0) {
        db_json_bool(&j, "setup_done", true);
    }
    if (lang[0]) {
        db_json_str(&j, "lang", lang);
    }
    db_json_obj_close(&j);
    if (!db_json_end(&j)) {
        send_error(E_UNKNOWN);
        return;
    }
    reply[0] = '\0';
    if ((strcmp(json, "{}") == 0 ? 200 : save()) == 200) {
        const char *s[] = {strstr(reply, "\"restart\":true") ? "restart" : "ok"};
        result(CMD_SETUP, s, 1);
        return;
    }
    cJSON *root = cJSON_Parse(reply);
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    const char *s[] = {"error", cJSON_IsString(err) ? err->valuestring : "saving failed"};
    result(CMD_SETUP, s, 2);
    cJSON_Delete(root);
}

static bool settings_open(void)
{
    return !db_config()->setup_done || esp_timer_get_time() - started_us < open_us;
}

static void handle(const db_improv_packet_t *p)
{
    uint8_t cmd, len;
    const uint8_t *d;
    if (!db_improv_rpc(p, &cmd, &d, &len)) {
        if (p->type == DB_IMPROV_RPC) {
            send_error(E_INVALID);
        }
        return;
    }
    if (last_error != E_NONE) {
        send_error(E_NONE);
    }
    bool sets = cmd == CMD_WIFI || cmd == CMD_SCAN || cmd == CMD_SETUP ||
                ((cmd == CMD_HOSTNAME || cmd == CMD_NAME) && len);
    if (sets && !settings_open()) {
        send_error(E_CLOSED);
        return;
    }
    switch (cmd) {
    case CMD_WIFI:
        wifi(d, len);
        break;
    case CMD_STATE:
        send_state(CMD_STATE);
        break;
    case CMD_INFO: {
        const char *s[] = {"DeckConnector", esp_app_get_description()->version, "ESP32-P4",
                           db_config()->name};
        result(CMD_INFO, s, 4);
        break;
    }
    case CMD_SCAN:
        scan();
        break;
    case CMD_HOSTNAME: /* the name is the host name too */
    case CMD_NAME:
        if (len) {
            name(cmd, d, len);
        } else {
            const char *s[] = {db_config()->name};
            result(cmd, s, 1);
        }
        break;
    case CMD_NETWORK:
        network();
        break;
    case CMD_SETUP:
        setup(d, len);
        break;
    default:
        send_error(E_UNKNOWN_CMD);
    }
}

static void improv_task(void *arg)
{
    static db_improv_parser_t parser;
    uint8_t buf[64];
    for (;;) {
        int n = port_read(buf, sizeof(buf));
        for (int i = 0; i < n; i++) {
            db_improv_packet_t p;
            db_improv_feed_t r = db_improv_feed(&parser, buf[i], &p);
            if (r == DB_IMPROV_PACKET) {
                handle(&p);
            } else if (r == DB_IMPROV_BAD_CHECKSUM) {
                send_error(E_INVALID);
            }
        }
        if (joining) {
            join_done();
        }
    }
}

esp_err_t db_serial_start(void)
{
    started_us = esp_timer_get_time();
    esp_err_t err = port_open();
    if (err == ESP_OK && xTaskCreate(improv_task, "improv", 4096, NULL, 2, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    return err;
}
