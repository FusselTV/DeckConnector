#include "db_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "db_config.h"
#include "db_json.h"
#include "db_net.h"
#include "db_ota.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "web.h"
#if !CONFIG_IDF_TARGET_LINUX /* the simulator has no TLS, no mDNS and no app image */
#include "esp_app_desc.h"
#include "esp_https_server.h"
#include "mdns.h"
#endif

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192
#define STATUS_JSON_MAX 4096

static const char *TAG = "web";

extern const char index_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_gz_end[] asm("_binary_index_html_gz_end");

httpd_uri_t web_uris[36];
route_t web_routes[36];
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

/* The page itself is public; it shows the login form until /api/status answers. It comes from the `www`
 * partition when one is there, else from the firmware; its ETag is that copy's hash, so a browser that has it
 * gets 304. Only the gzip copy is kept: every browser takes it. */
static esp_err_t page(httpd_req_t *req)
{
    char etag[20];
    db_ota_web_t web;
    bool own = db_ota_web(&web);
#if CONFIG_IDF_TARGET_LINUX
    static const uint8_t app_sha[8] = {0}; /* the simulator restarts with every build */
#else
    const uint8_t *app_sha = esp_app_get_description()->app_elf_sha256;
#endif
    const uint8_t *sha = own ? web.sha : app_sha;
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
    return own ? httpd_resp_send(req, (const char *)web.gz, web.len)
               : httpd_resp_send(req, index_gz_start, index_gz_end - index_gz_start);
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

bool web_tls_running(void)
{
    return tls_server != NULL;
}

bool web_plain_refused(httpd_req_t *req)
{
    if (!db_config()->https_only || web_tls(req) || !tls_server) { /* no HTTPS: plain stays, or nobody gets in */
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

static const char *const LEVELS[] = {"open", "control", "admin"};

/* Every route and who may call it, for scripts and other frontends. */
static size_t api_json(char *buf, size_t cap)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    for (size_t i = 0; i < web_route_count; i++) {
        db_json_obj_open(&j, NULL);
        db_json_str(&j, "method", http_method_str(web_uris[i].method));
        db_json_str(&j, "path", web_uris[i].uri);
        db_json_str(&j, "access", LEVELS[web_routes[i].level]);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    return db_json_end(&j);
}

static esp_err_t get_api(httpd_req_t *req)
{
    return send_built(req, 4096, api_json);
}

/* A page from an origin in cors_origins may call the API with a token. Cookies never go cross-origin: no
 * Allow-Credentials. The headers point into `origin`, which must live until the reply is sent. */
static bool cors(httpd_req_t *req, char origin[96])
{
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, 96) != ESP_OK || !origin[0]) {
        return false;
    }
    const char *list = db_config()->cors_origins;
    size_t n = strlen(origin);
    for (const char *p = list; (p = strstr(p, origin)) != NULL; p += n) {
        if ((p == list || p[-1] == ',' || p[-1] == ' ') && (p[n] == '\0' || p[n] == ',' || p[n] == ' ')) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin);
            httpd_resp_set_hdr(req, "Vary", "Origin");
            return true;
        }
    }
    return false;
}

static esp_err_t cors_preflight(httpd_req_t *req)
{
    char origin[96];
    if (!cors(req, origin)) {
        return send_error(req, "403 Forbidden", "origin not in cors_origins");
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
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
    char origin[96];
    cors(req, origin);
    esp_err_t err = r->level == WEB_OPEN || auth_check(req, r->level) ? r->handler(req) : ESP_OK;
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

#if !CONFIG_IDF_TARGET_LINUX
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

#endif

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

/* The OPTIONS route matches every API path, so the server says 405 also for a path that does not exist. */
static esp_err_t wrong_method(httpd_req_t *req, httpd_err_code_t code)
{
    size_t len = strcspn(req->uri, "?");
    for (size_t i = 0; i < web_route_count; i++) {
        if (web_uris[i].method != HTTP_OPTIONS && httpd_uri_match_wildcard(web_uris[i].uri, req->uri, len)) {
            return send_error(req, "405 Method Not Allowed", "method not allowed here");
        }
    }
    return send_error(req, "404 Not Found", "no such endpoint");
}

#if !CONFIG_IDF_TARGET_LINUX
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
    httpd_register_err_handler(tls_server, HTTPD_405_METHOD_NOT_ALLOWED, wrong_method);
}
#endif

const char *db_web_fingerprint(void)
{
    return fingerprint;
}

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap), size_t (*state)(char *buf, size_t cap))
{
    api_status_json = status;
    auth_start();
    handler_lock = xSemaphoreCreateMutex();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
#if CONFIG_IDF_TARGET_LINUX
    const char *port = getenv("DB_SIM_PORT"); /* one simulator per test worker */
    cfg.server_port = port ? (uint16_t)atoi(port) : 8080;
    cfg.ctrl_port = cfg.server_port + 10000;
#endif
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
    db_ota_web_load(DB_WEB_API);
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const struct {
        httpd_uri_t uri;
        web_level_t level;
    } list[] = {
        {{.uri = "/", .method = HTTP_GET, .handler = page}, WEB_OPEN},
        {{.uri = "/api", .method = HTTP_GET, .handler = get_api}, WEB_CONTROL},
        {{.uri = "/api/*", .method = HTTP_OPTIONS, .handler = cors_preflight}, WEB_OPEN},
        {{.uri = "/api/status", .method = HTTP_GET, .handler = get_status}, WEB_CONTROL},
        {{.uri = "/api/config", .method = HTTP_GET, .handler = get_config}, WEB_ADMIN},
        {{.uri = "/api/config", .method = HTTP_PUT, .handler = put_config}, WEB_ADMIN},
        {{.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota}, WEB_ADMIN},
        {{.uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot}, WEB_ADMIN},
        {{.uri = "/api/log", .method = HTTP_GET, .handler = get_log}, WEB_ADMIN},
        {{.uri = "/api/screen", .method = HTTP_GET, .handler = get_screen}, WEB_CONTROL},
        {{.uri = "/api/screen/*", .method = HTTP_GET, .handler = get_screen_image}, WEB_CONTROL},
        {{.uri = "/api/input", .method = HTTP_POST, .handler = post_input}, WEB_CONTROL},
        {{.uri = "/api/menu", .method = HTTP_POST, .handler = post_menu}, WEB_CONTROL},
        {{.uri = "/api/deck/reconnect", .method = HTTP_POST, .handler = post_deck_reconnect}, WEB_CONTROL},
        {{.uri = "/api/dock", .method = HTTP_POST, .handler = post_dock}, WEB_ADMIN},
        {{.uri = "/api/screen.jpg", .method = HTTP_GET, .handler = get_screen_jpg}, WEB_CONTROL},
        {{.uri = "/api/ota/url", .method = HTTP_POST, .handler = post_ota_url}, WEB_ADMIN},
        {{.uri = "/api/slots", .method = HTTP_GET, .handler = get_slots}, WEB_ADMIN},
        {{.uri = "/api/slots/*", .method = HTTP_POST, .handler = post_slot_boot}, WEB_ADMIN},
        {{.uri = "/api/tokens", .method = HTTP_GET, .handler = auth_tokens_list}, WEB_ADMIN},
        {{.uri = "/api/tokens", .method = HTTP_POST, .handler = auth_tokens_add}, WEB_ADMIN},
        {{.uri = "/api/tokens/*", .method = HTTP_DELETE, .handler = auth_tokens_remove}, WEB_ADMIN},
        /* its handler runs once per frame and checks the login at the handshake */
        {{.uri = "/api/ws",
          .method = HTTP_GET,
          .handler = events_handler,
          .is_websocket = true,
          .ws_pre_handshake_cb = events_check,
          .ws_post_handshake_cb = events_open},
         WEB_CONTROL},
        {{.uri = "/api/login", .method = HTTP_POST, .handler = auth_login}, WEB_OPEN},
        {{.uri = "/api/logout", .method = HTTP_POST, .handler = auth_logout}, WEB_OPEN},
        {{.uri = "/api/profile", .method = HTTP_GET, .handler = get_profile}, WEB_ADMIN},
        {{.uri = "/api/metrics", .method = HTTP_GET, .handler = get_metrics}, WEB_CONTROL},
        {{.uri = "/api/frontend", .method = HTTP_GET, .handler = get_frontend}, WEB_CONTROL},
        {{.uri = "/api/companion/found", .method = HTTP_GET, .handler = get_companions}, WEB_ADMIN},
        {{.uri = "/api/frontend", .method = HTTP_PUT, .handler = put_frontend}, WEB_ADMIN},
        {{.uri = "/api/frontend", .method = HTTP_DELETE, .handler = delete_frontend}, WEB_ADMIN},
        {{.uri = "/api/coredump", .method = HTTP_GET, .handler = get_coredump}, WEB_ADMIN},
        {{.uri = "/api/boot", .method = HTTP_GET, .handler = get_boot}, WEB_ADMIN},
        {{.uri = "/api/coredump", .method = HTTP_DELETE, .handler = delete_coredump}, WEB_ADMIN},
#ifdef DB_DEBUG_CRASH
        {{.uri = "/api/debug/crash", .method = HTTP_POST, .handler = post_debug_crash}, WEB_ADMIN},
#endif
    };
    _Static_assert(sizeof(list) / sizeof(list[0]) <= sizeof(web_uris) / sizeof(web_uris[0]),
                   "more routes than slots");
    web_route_count = sizeof(list) / sizeof(list[0]);
    for (size_t i = 0; i < web_route_count; i++) {
        web_uris[i] = list[i].uri;
        web_routes[i].level = list[i].level;
        if (!list[i].uri.is_websocket) {
            web_routes[i].handler = list[i].uri.handler;
            web_uris[i].handler = timed;
            web_uris[i].user_ctx = &web_routes[i];
        }
        httpd_register_uri_handler(server, &web_uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, wrong_method);
    events_start(state);
#if CONFIG_IDF_TARGET_LINUX
    (void)fingerprint;
#else
    start_tls();
#endif
    metrics_start();
#if !CONFIG_IDF_TARGET_LINUX
    announce_api();
#endif
    ESP_LOGI(TAG, "web UI on port 80%s", tls_server ? " and 443" : "");
    return ESP_OK;
}
