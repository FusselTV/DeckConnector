#include "db_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "db_config.h"
#include "db_net.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "web.h"

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192
#define STATUS_JSON_MAX 4096

static const char *TAG = "web";

extern const char index_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_gz_end[] asm("_binary_index_html_gz_end");

httpd_uri_t web_uris[24];
route_t web_routes[24];
size_t web_route_count;
static SemaphoreHandle_t handler_lock;

void web_lock(void)
{
    xSemaphoreTake(handler_lock, portMAX_DELAY);
}

void web_unlock(void)
{
    xSemaphoreGive(handler_lock);
}

esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf);
}

int read_body(httpd_req_t *req, char *buf, size_t cap)
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
    vTaskDelay(pdMS_TO_TICKS(1000)); /* the reply goes out first */
    esp_restart();
}

void restart_soon(void)
{
    xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
}

/* The page itself is public; it shows the login form until /api/status answers. It only changes with the
 * firmware, so its ETag is the firmware's hash and a browser that has it gets 304. Only the gzip copy is
 * kept: every browser takes it. */
static esp_err_t page(httpd_req_t *req)
{
    char etag[20];
    const uint8_t *sha = esp_app_get_description()->app_elf_sha256;
    snprintf(etag, sizeof(etag), "\"%02x%02x%02x%02x%02x%02x%02x%02x\"", sha[0], sha[1], sha[2], sha[3],
             sha[4], sha[5], sha[6], sha[7]);
    char given[sizeof(etag)] = {0};
    httpd_req_get_hdr_value_str(req, "If-None-Match", given, sizeof(given));
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (strcmp(given, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, NULL, 0);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, index_gz_start, index_gz_end - index_gz_start);
}

/* A reply built into a heap buffer; build returns 0 when it does not fit. */
esp_err_t send_built(httpd_req_t *req, size_t cap, size_t (*build)(char *buf, size_t cap))
{
    char *buf = malloc(cap);
    size_t len = buf ? build(buf, cap) : 0;
    esp_err_t err = len ? send_json(req, buf)
                        : send_error(req, "500 Internal Server Error", buf ? "too large" : "memory");
    free(buf);
    return err;
}

static httpd_handle_t tls_server;
static char fingerprint[96];

bool web_tls(httpd_req_t *req)
{
    return tls_server && req->handle == tls_server;
}

bool web_plain_refused(httpd_req_t *req)
{
    if (!db_config()->https_only || web_tls(req)) {
        return false;
    }
    db_net_status_t n;
    db_net_status(&n);
    return !n.ap_active; /* phones look for the setup page over plain HTTP */
}

/* A browser opening the page goes over to HTTPS; everything else gets nothing, a password included. 302, not
 * 301: a browser keeps a 301 after "nur HTTPS" is turned off again. */
static esp_err_t refuse_plain(httpd_req_t *req)
{
    char host[64];
    if (req->method == HTTP_GET && strcmp(req->uri, "/") == 0 &&
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        char *port = strchr(host, ':');
        if (port) {
            *port = '\0';
        }
        char to[80];
        snprintf(to, sizeof(to), "https://%s/", host);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", to);
        return httpd_resp_send(req, NULL, 0);
    }
    return send_error(req, "403 Forbidden", "HTTPS only");
}

static esp_err_t timed(httpd_req_t *req)
{
    if (web_plain_refused(req)) {
        return refuse_plain(req);
    }
    route_t *r = req->user_ctx;
    bool lock = r->handler != post_ota; /* an upload takes seconds and shares nothing */
    if (lock) {
        web_lock();
    }
    int64_t start = esp_timer_get_time();
    esp_err_t err = r->open || auth_check(req) ? r->handler(req) : ESP_OK;
    uint32_t us = (uint32_t)(esp_timer_get_time() - start);
    r->count++;
    r->total_us += us;
    r->max_us = us > r->max_us ? us : r->max_us;
    if (lock) {
        web_unlock();
    }
    return err;
}

/* Without it a small reply waits for the client's delayed ACK of the headers: 40 ms instead of 1. */
static esp_err_t no_delay(httpd_handle_t hd, int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return ESP_OK;
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

static void start_tls(void)
{
    char *cert, *key;
    if (!web_cert(db_config()->name, &cert, &key, fingerprint)) {
        ESP_LOGE(TAG, "no certificate, no HTTPS");
        return;
    }
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.servercert = (const uint8_t *)cert;
    cfg.servercert_len = strlen(cert) + 1;
    cfg.prvtkey_pem = (const uint8_t *)key;
    cfg.prvtkey_len = strlen(key) + 1;
    cfg.session_tickets = true;
    cfg.httpd.max_uri_handlers = web_route_count;
    cfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
    cfg.httpd.keep_alive_enable = true;
    cfg.httpd.keep_alive_idle = 5;
    cfg.httpd.keep_alive_interval = 5;
    cfg.httpd.keep_alive_count = 3;
    esp_err_t err = httpd_ssl_start(&tls_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS: %s", esp_err_to_name(err));
        tls_server = NULL;
        return;
    }
    for (size_t i = 0; i < web_route_count; i++) {
        httpd_register_uri_handler(tls_server, &web_uris[i]);
    }
    httpd_register_err_handler(tls_server, HTTPD_404_NOT_FOUND, not_found);
}

const char *db_web_fingerprint(void)
{
    return fingerprint;
}

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap), size_t (*state)(char *buf, size_t cap))
{
    api_status_json = status;
    handler_lock = xSemaphoreCreateMutex();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = sizeof(web_uris) / sizeof(web_uris[0]);
    /* several Home Assistants hold a socket and a web socket each; with the dock and the links this stays
     * under CONFIG_LWIP_MAX_SOCKETS */
    cfg.max_open_sockets = 12;
    cfg.backlog_conn = cfg.max_open_sockets; /* a refused SYN costs the client a 1 s retry */
    cfg.open_fn = no_delay;
    cfg.keep_alive_enable = true; /* a client gone without closing shows after about 20 s, not never */
    cfg.keep_alive_idle = 5;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count = 3;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout =
        2; /* a client that stops halfway through its headers blocks the server this long */
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t list[] = {
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
        {.uri = "/api/deck/reconnect", .method = HTTP_POST, .handler = post_deck_reconnect},
        {.uri = "/api/dock", .method = HTTP_POST, .handler = post_dock},
        {.uri = "/api/screen.jpg", .method = HTTP_GET, .handler = get_screen_jpg},
        {.uri = "/api/ota/url", .method = HTTP_POST, .handler = post_ota_url},
        {.uri = "/api/slots", .method = HTTP_GET, .handler = get_slots},
        {.uri = "/api/slots/*", .method = HTTP_POST, .handler = post_slot_boot},
        {.uri = "/api/ws",
         .method = HTTP_GET,
         .handler = events_handler,
         .is_websocket = true,
         .ws_pre_handshake_cb = events_check,
         .ws_post_handshake_cb = events_open},
        {.uri = "/api/login", .method = HTTP_POST, .handler = auth_login},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = auth_logout},
        {.uri = "/api/profile", .method = HTTP_GET, .handler = get_profile},
        {.uri = "/api/metrics", .method = HTTP_GET, .handler = get_metrics},
    };
    _Static_assert(sizeof(list) <= sizeof(web_uris), "more routes than slots");
    web_route_count = sizeof(list) / sizeof(list[0]);
    for (size_t i = 0; i < web_route_count; i++) {
        web_uris[i] = list[i];
        if (!list[i]
                 .is_websocket) { /* its handler runs once per frame, and checks the login at the handshake */
            web_routes[i].open =
                list[i].handler == page || list[i].handler == auth_login || list[i].handler == auth_logout;
            web_routes[i].handler = list[i].handler;
            web_uris[i].handler = timed;
            web_uris[i].user_ctx = &web_routes[i];
        }
        httpd_register_uri_handler(server, &web_uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    events_start(state);
    start_tls();
    metrics_start();
    announce_api();
    ESP_LOGI(TAG, "web UI on port 80%s", tls_server ? " and 443" : "");
    return ESP_OK;
}
