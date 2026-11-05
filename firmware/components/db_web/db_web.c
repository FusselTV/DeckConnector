#include "db_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "cJSON.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_screen.h"
#include "db_ui.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192
#define STATUS_JSON_MAX 4096

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static size_t (*status_json)(char *buf, size_t cap);

static esp_err_t send_json(httpd_req_t *req, const char *json, size_t len)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (ssize_t)len);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[200];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf, (size_t)n);
}

static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len >= cap) {
        return -1;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    buf[got] = '\0';
    return (int)got;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* The page itself is public; it shows the login form until /api/status answers. */
static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1); /* EMBED_TXTFILES adds a NUL */
}

static esp_err_t get_status(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char *buf = malloc(STATUS_JSON_MAX);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    size_t len = status_json(buf, STATUS_JSON_MAX);
    esp_err_t err =
        len ? send_json(req, buf, len) : send_error(req, "500 Internal Server Error", "status too large");
    free(buf);
    return err;
}

static esp_err_t get_config(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char buf[JSON_MAX];
    return send_json(req, buf, db_config_to_json(db_config(), false, buf, sizeof(buf)));
}

static const char *status_line(int code)
{
    switch (code) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 409:
        return "409 Conflict";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

static esp_err_t reply_with(httpd_req_t *req, int code, const char *reply)
{
    httpd_resp_set_status(req, status_line(code));
    return send_json(req, reply, strlen(reply));
}

static esp_err_t put_config(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    char reply[160];
    return reply_with(req, api_config(body, reply, sizeof(reply)), reply);
}

static esp_err_t post_input(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char body[256];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    char reply[160];
    return reply_with(req, api_input(body, reply, sizeof(reply)), reply);
}

static esp_err_t post_menu(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    if (db_ui_open_menu() != ESP_OK) {
        return send_error(req, "409 Conflict", "no deck connected");
    }
    return send_json(req, "{\"ok\":true}", 11);
}

static esp_err_t post_ota(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    if (db_ota_begin(req->content_len) != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running or the file is empty");
    }
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_INTERNAL);
    size_t left = req->content_len;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    int timeouts = 0;
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(req, (char *)buf, left < CHUNK ? left : CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue; /* a vanished client must not hold the update lock forever */
        }
        if (n <= 0) {
            err = ESP_FAIL;
            db_ota_abort("upload interrupted");
            break;
        }
        err = db_ota_write(buf, (size_t)n);
        left -= (size_t)n;
    }
    heap_caps_free(buf);
    if (err == ESP_OK) {
        err = db_ota_end();
    }
    if (err != ESP_OK) {
        db_ota_abort(esp_err_to_name(err));
        return send_error(req, "400 Bad Request", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"restart\":true}", 26);
}

static esp_err_t post_reboot(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    send_json(req, "{\"ok\":true}", 11);
    xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t get_log(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char *buf = heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    size_t n = db_log_read(buf, 16 * 1024);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, (ssize_t)n);
    heap_caps_free(buf);
    return err;
}

static void write_rect(db_json_t *j, db_rect_t r)
{
    db_json_obj_open(j, NULL);
    db_json_int(j, "x", r.x);
    db_json_int(j, "y", r.y);
    db_json_int(j, "w", r.w);
    db_json_int(j, "h", r.h);
    db_json_obj_close(j);
}

static esp_err_t get_screen(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    db_device_info_t *dev = malloc(sizeof(*dev));
    db_shot_t *shots = malloc(DB_SCREEN_MAX * sizeof(*shots));
    char *buf = malloc(SCREEN_JSON_MAX);
    if (!dev || !shots || !buf) {
        free(dev);
        free(shots);
        free(buf);
        return send_error(req, "500 Internal Server Error", "memory");
    }
    db_source_t writer;
    size_t n = db_deck_shots(&writer, shots, DB_SCREEN_MAX);
    bool present = db_deck_info(dev);
    db_json_t j;
    db_json_begin(&j, buf, SCREEN_JSON_MAX);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "writer", db_deck_source_name(writer));
    if (present) {
        const db_display_t *d = &dev->displays[0];
        db_json_int(&j, "width", d->width);
        db_json_int(&j, "height", d->height);
        db_json_arr_open(&j, "keys");
        for (uint8_t k = 0; k < dev->key_count; k++) {
            for (uint8_t t = 0; t < d->target_count; t++) {
                if (d->targets[t].kind == DB_TARGET_KEY && d->targets[t].index == k) {
                    write_rect(&j, d->targets[t].rect);
                }
            }
        }
        db_json_arr_close(&j);
        db_json_arr_open(&j, "touch");
        for (uint8_t t = 0; t < dev->touch_count; t++) {
            write_rect(&j, dev->touch[t].rect);
        }
        db_json_arr_close(&j);
        db_json_int(&j, "encoders", dev->encoder_count);
        int last;
        bool on_encoders = db_ui_menu_chord(dev->encoder_count, dev->key_count, &last);
        db_json_obj_open(&j, "menu_chord");
        db_json_str(&j, "on", on_encoders ? "encoders" : "keys");
        db_json_int(&j, "first", 0);
        db_json_int(&j, "last", last);
        db_json_obj_close(&j);
    }
    db_json_arr_open(&j, "images");
    for (size_t i = 0; present && i < n; i++) {
        char url[32];
        snprintf(url, sizeof(url), "/api/screen/%lu.jpg", (unsigned long)shots[i].id);
        db_json_obj_open(&j, NULL);
        db_json_int(&j, "x", shots[i].rect.x);
        db_json_int(&j, "y", shots[i].rect.y);
        db_json_int(&j, "w", shots[i].rect.w);
        db_json_int(&j, "h", shots[i].rect.h);
        db_json_str(&j, "url", url);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    size_t len = db_json_end(&j);
    esp_err_t err =
        len ? send_json(req, buf, len) : send_error(req, "500 Internal Server Error", "too many images");
    free(dev);
    free(shots);
    free(buf);
    return err;
}

/* An image never changes under its id, so browsers may keep it. */
static esp_err_t get_screen_image(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    unsigned long id = strtoul(req->uri + strlen("/api/screen/"), NULL, 10);
    uint8_t *data = NULL;
    size_t len = 0;
    esp_err_t found = db_deck_shot_copy((uint32_t)id, &data, &len);
    if (found == ESP_ERR_NO_MEM) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    if (found != ESP_OK) {
        return send_error(req, "404 Not Found", "image no longer shown");
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "private, max-age=3600, immutable");
    esp_err_t err = httpd_resp_send(req, (const char *)data, (ssize_t)len);
    heap_caps_free(data);
    return err;
}

/* The picture on the deck as one JPEG, for a camera in Home Assistant. */
static esp_err_t get_screen_jpg(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = db_ui_snapshot(&jpeg, &len);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "no Stream Deck attached");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send(req, (const char *)jpeg, (ssize_t)len);
    heap_caps_free(jpeg);
    return err;
}

static esp_err_t post_ota_url(httpd_req_t *req)
{
    if (!auth_check(req)) {
        return ESP_OK;
    }
    char body[256];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    bool ok = cJSON_IsString(url) && strncmp(url->valuestring, "http://", 7) == 0;
    esp_err_t err = ok ? db_ota_start_url(url->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "url: http://… to a signed .dbfw file expected");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running");
    }
    /* progress shows on the deck and in the state of /api/ws; the bridge restarts when done */
    return send_json(req, "{\"ok\":true}", 11);
}

/* Home Assistant and scripts find the bridge's API by this. */
static void announce_api(void)
{
    const char *fw = esp_app_get_description()->version;
    mdns_txt_item_t txt[] = {{"path", "/api"}, {"fw", fw}};
    esp_err_t err = mdns_service_add(NULL, "_deckbridge-api", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS API announcement failed: %s", esp_err_to_name(err));
    }
}

/* Phones probe some URL when they join a WLAN; sending them to the page opens it by itself. */
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    db_net_status_t n;
    db_net_status(&n);
    if (strncmp(req->uri, "/api/", 5) == 0) {
        return send_error(req, "404 Not Found", "no such endpoint");
    }
    if (!n.ap_active) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    char loc[40];
    snprintf(loc, sizeof(loc), "http://%s/", n.ap_ip);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, NULL, 0);
}

/* Answer every A query with the address of the setup AP while it is on. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "captive DNS not available");
        vTaskDelete(NULL);
    }
    uint8_t pkt[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, pkt, sizeof(pkt) - 16, 0, (struct sockaddr *)&from, &from_len);
        db_net_status_t st;
        db_net_status(&st);
        if (n < 12 || !st.ap_active) {
            continue;
        }
        /* Keep only header and question: an EDNS record after the question would hide our answer. */
        int q = 12;
        while (q < n && pkt[q] != 0) {
            q += pkt[q] + 1;
        }
        q += 5; /* zero label, type, class */
        if (q > n) {
            continue;
        }
        n = q;
        memset(pkt + 8, 0, 4); /* no authority or additional records */
        struct in_addr ip;
        inet_aton(st.ap_ip, &ip);
        pkt[2] = 0x81; /* response, recursion desired */
        pkt[3] = 0x80; /* recursion available, no error */
        pkt[7] = 1;    /* one answer */
        const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
        memcpy(pkt + n, answer, sizeof(answer));
        memcpy(pkt + n + sizeof(answer), &ip.s_addr, 4);
        sendto(sock, pkt, (size_t)n + sizeof(answer) + 4, 0, (struct sockaddr *)&from, from_len);
    }
}

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap), size_t (*state)(char *buf, size_t cap))
{
    status_json = status;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 20;
    cfg.max_open_sockets =
        5; /* plus 2 * DB_DOCK_HOSTS dock sockets and the links, under CONFIG_LWIP_MAX_SOCKETS */
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout =
        5; /* a client that stops halfway through its headers blocks the server this long */
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = get_status},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config},
        {.uri = "/api/config", .method = HTTP_PUT, .handler = put_config},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot},
        {.uri = "/api/log", .method = HTTP_GET, .handler = get_log},
        {.uri = "/api/screen", .method = HTTP_GET, .handler = get_screen},
        {.uri = "/api/screen/*", .method = HTTP_GET, .handler = get_screen_image},
        {.uri = "/api/input", .method = HTTP_POST, .handler = post_input},
        {.uri = "/api/menu", .method = HTTP_POST, .handler = post_menu},
        {.uri = "/api/screen.jpg", .method = HTTP_GET, .handler = get_screen_jpg},
        {.uri = "/api/ota/url", .method = HTTP_POST, .handler = post_ota_url},
        {.uri = "/api/ws",
         .method = HTTP_GET,
         .handler = events_handler,
         .is_websocket = true,
         .ws_pre_handshake_cb = events_check,
         .ws_post_handshake_cb = events_open},
        {.uri = "/api/login", .method = HTTP_POST, .handler = auth_login},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = auth_logout},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    events_start(server, state);
    announce_api();
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "web UI on port 80");
    return ESP_OK;
}
