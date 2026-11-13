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
#include "db_ui.h"
#include "esp_attr.h"
#include "esp_check.h"
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

static const char *TAG = "web";

extern const char index_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_gz_end[] asm("_binary_index_html_gz_end");

httpd_uri_t web_uris[WEB_ROUTES_MAX];
route_t web_routes[WEB_ROUTES_MAX];
size_t web_route_count;
static SemaphoreHandle_t handler_lock;

bool web_lock_for(uint32_t ms)
{
    return xSemaphoreTake(handler_lock, pdMS_TO_TICKS(ms)) == pdTRUE;
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

/* Matched by prefix: a text may carry details after it. */
const char *db_web_error_code(const char *msg)
{
    static const char *const codes[][2] = {
        {"ota_too_short", "file too short"},
        {"ota_not_dbfw", "not a .dbfw file"},
        {"ota_not_signed", "not signed for this DeckConnector"},
        {"ota_wrong_target", "unknown target"},
        {"ota_wrong_target", "not a bootloader for this chip"},
        {"ota_too_old", "below the security level"},
        {"url_http_only", "url: http://"},
        {"no_releases", "no releases yet"},
        {"probe_no_name", "name not found"},
        {"probe_refused", "connection refused"},
        {"probe_timeout", "no connection within"}, /* before the shorter "no connection" */
        {"probe_no_connection", "no connection"},
        {"probe_cert", "certificate not trusted"},
        {"probe_tls", "TLS failed"},
        {"probe_not_ws", "not a WebSocket"},
        {"probe_no_answer", "no answer within"},
        {"probe_not_companion", "it answers, but it is not Companion"},
        {"strip_outside", "x and y (and x2, y2 for a swipe) must lie on the touch strip"},
        {"busy", "busy"},
        {"wrong_password", "wrong password"},
        {"needs_password", "needs the password, not a token"},
    };
    for (size_t i = 0; msg && i < sizeof(codes) / sizeof(codes[0]); i++) {
        if (strncmp(msg, codes[i][1], strlen(codes[i][1])) == 0) {
            return codes[i][0];
        }
    }
    return NULL;
}

esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[240];
    const char *code = db_web_error_code(msg);
    if (code) {
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\",\"code\":\"%s\"}", msg, code);
    } else {
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    }
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

void restart_soon(void)
{
    db_ota_restart(1000);
}

/* The page itself is public; it shows the login form until /api/status answers. It comes from the `www`
 * partition when one is there, else from the firmware; its ETag is that copy's hash, so a browser that has it
 * gets 304. */
static esp_err_t page(httpd_req_t *req)
{
    char query[48];
    char token[20];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "t", token, sizeof(token)) == ESP_OK) {
        return auth_qr_login(req, token);
    }
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

/* A server task stuck in a handler takes the whole web interface and API with it, and nothing else notices.
 * Each server runs an empty job every 10 s; none for 2 min restarts the bridge. A firmware upload takes 10 to
 * 30 s. */
#define HUNG_US        (120 * 1000000LL)
#define HUNG_RESTARTED 0x3eb0de4d
#if CONFIG_IDF_TARGET_LINUX
#undef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
static RTC_NOINIT_ATTR uint32_t hung_restarted; /* kept over a restart, not over a power cut */
static struct {
    httpd_handle_t hd;
    volatile int64_t ran;
    volatile bool proven; /* ran a job since the start */
    int64_t queued; /* one job waits at a time: the server's control socket holds 6 and the live deck needs
                       them */
} alive[2];

static void alive_job(void *arg)
{
    typeof(alive[0]) *a = arg;
    a->ran = esp_timer_get_time();
    a->proven = true;
}

/* One restart per power-on, armed again after an hour of both servers running: a server that hangs again
 * right after it keeps the rest of the bridge running. */
static void alive_check(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (hung_restarted == HUNG_RESTARTED && now > 3600 * 1000000LL && db_web_alive()) {
        hung_restarted = 0;
    }
    for (int i = 0; i < 2 && alive[i].hd; i++) {
        if (now - alive[i].ran > HUNG_US && hung_restarted != HUNG_RESTARTED) {
            hung_restarted = HUNG_RESTARTED;
            ESP_LOGE(TAG, "the %s server ran nothing for 2 min: restarting", i ? "second" : "first");
            db_ota_restart(0);
            return;
        }
        if (alive[i].ran >= alive[i].queued ||
            now - alive[i].queued > 30 * 1000000LL) { /* or it was dropped */
            alive[i].queued = now;
            httpd_queue_work(alive[i].hd, alive_job, &alive[i]);
        }
    }
}

bool db_web_alive(void)
{
    return alive[0].hd && alive[0].proven && (!alive[1].hd || alive[1].proven);
}

static void watch(httpd_handle_t hd)
{
    static esp_timer_handle_t timer;
    int i = alive[0].hd != NULL;
    alive[i].ran = esp_timer_get_time();
    alive[i].hd = hd;
    if (!timer) {
        esp_timer_create_args_t a = {.callback = alive_check, .name = "web_alive"};
        if (esp_timer_create(&a, &timer) == ESP_OK) {
            esp_timer_start_periodic(timer, 10 * 1000000LL);
        }
    }
}
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
    if (!db_config()->https_only || web_tls(req) ||
        !tls_server) { /* no HTTPS: plain stays, or nobody gets in */
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

static const char *const LEVELS[] = {"open", "control", "full", "admin"};

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
static bool cors_allowed(httpd_req_t *req, char origin[96])
{
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, 96) != ESP_OK || !origin[0]) {
        return false;
    }
    const char *list = db_config()->cors_origins;
    size_t n = strlen(origin);
    for (const char *p = list; (p = strstr(p, origin)) != NULL; p += n) {
        if ((p == list || p[-1] == ',' || p[-1] == ' ') && (p[n] == '\0' || p[n] == ',' || p[n] == ' ')) {
            return true;
        }
    }
    return false;
}

static void cors(httpd_req_t *req, char origin[96])
{
    if (cors_allowed(req, origin)) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin);
        httpd_resp_set_hdr(req, "Vary", "Origin");
    }
}

/* timed() already set Allow-Origin: a second copy makes browsers refuse the reply. */
static esp_err_t cors_preflight(httpd_req_t *req)
{
    char origin[96];
    if (!cors_allowed(req, origin)) {
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
    /* a handler that hangs must not take every other request on both ports with it */
    if (lock && !web_lock_for(10000)) {
        return send_error(req, "503 Service Unavailable", "busy");
    }
    int64_t start = esp_timer_get_time();
    char origin[96];
    cors(req, origin);
    bool allowed = (r->level == WEB_OPEN || auth_check(req, r->level)) && !(r->deck && web_deck_refused(req));
    esp_err_t err = allowed ? r->handler(req) : ESP_OK;
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
    esp_err_t err =
        mdns_service_add(NULL, "_deckconnector-api", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
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
    cfg.httpd.max_open_sockets = 12; /* as port 80 */
    cfg.httpd.backlog_conn = cfg.httpd.max_open_sockets;
    cfg.httpd.lru_purge_enable = true;
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
    watch(tls_server);
    httpd_register_err_handler(tls_server, HTTPD_404_NOT_FOUND, not_found);
    httpd_register_err_handler(tls_server, HTTPD_405_METHOD_NOT_ALLOWED, wrong_method);
}
#endif

const char *db_web_fingerprint(void)
{
    return fingerprint;
}

bool db_web_cert_custom(void)
{
#if CONFIG_IDF_TARGET_LINUX
    return false;
#else
    return web_cert_custom();
#endif
}

bool db_web_cert_unread(void)
{
#if CONFIG_IDF_TARGET_LINUX
    return false;
#else
    return web_cert_custom_unread();
#endif
}

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap), size_t (*state)(char *buf, size_t cap))
{
    api_status_json = status;
    auth_start();
    db_ui_setup_step(db_web_setup_step());
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
    /* every closed browser tab logged a dozen "error in send" warnings and pushed real lines out of the log
     * ring */
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_ws", ESP_LOG_ERROR);
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    watch(server);
    static esp_err_t (*const deck_routes[])(httpd_req_t *) = {
        get_screen, get_screen_image, get_screen_jpg,      post_input,
        post_menu,  delete_menu,      post_deck_reconnect, post_deck_stall};
    const struct {
        httpd_uri_t uri;
        web_level_t level;
    } list[] = {
        {{.uri = "/", .method = HTTP_GET, .handler = page}, WEB_OPEN},
        {{.uri = "/api", .method = HTTP_GET, .handler = get_api}, WEB_CONTROL},
        {{.uri = "/api/*", .method = HTTP_OPTIONS, .handler = cors_preflight}, WEB_OPEN},
        {{.uri = "/api/status", .method = HTTP_GET, .handler = get_status}, WEB_CONTROL},
        {{.uri = "/api/config", .method = HTTP_GET, .handler = get_config}, WEB_FULL},
        {{.uri = "/api/config", .method = HTTP_PUT, .handler = put_config}, WEB_FULL},
        {{.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota}, WEB_FULL},
        {{.uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot}, WEB_FULL},
        {{.uri = "/api/reset", .method = HTTP_POST, .handler = post_reset}, WEB_ADMIN},
        {{.uri = "/api/log", .method = HTTP_GET, .handler = get_log}, WEB_FULL},
        {{.uri = "/api/screen", .method = HTTP_GET, .handler = get_screen}, WEB_CONTROL},
        {{.uri = "/api/screen/*", .method = HTTP_GET, .handler = get_screen_image}, WEB_CONTROL},
        {{.uri = "/api/input", .method = HTTP_POST, .handler = post_input}, WEB_CONTROL},
        {{.uri = "/api/menu", .method = HTTP_POST, .handler = post_menu}, WEB_CONTROL},
        {{.uri = "/api/menu", .method = HTTP_DELETE, .handler = delete_menu}, WEB_CONTROL},
        {{.uri = "/api/deck/reconnect", .method = HTTP_POST, .handler = post_deck_reconnect}, WEB_CONTROL},
        {{.uri = "/api/deck/stall", .method = HTTP_POST, .handler = post_deck_stall}, WEB_FULL},
        {{.uri = "/api/dock", .method = HTTP_POST, .handler = post_dock}, WEB_FULL},
        {{.uri = "/api/pairing/denied/*", .method = HTTP_DELETE, .handler = delete_denied}, WEB_FULL},
        {{.uri = "/api/screen.jpg", .method = HTTP_GET, .handler = get_screen_jpg}, WEB_CONTROL},
        {{.uri = "/api/ota/url", .method = HTTP_POST, .handler = post_ota_url}, WEB_FULL},
        {{.uri = "/api/slots", .method = HTTP_GET, .handler = get_slots}, WEB_FULL},
        {{.uri = "/api/slots/*", .method = HTTP_POST, .handler = post_slot_boot}, WEB_FULL},
        {{.uri = "/api/tokens", .method = HTTP_GET, .handler = auth_tokens_list}, WEB_ADMIN},
        {{.uri = "/api/backup", .method = HTTP_GET, .handler = get_backup}, WEB_ADMIN},
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
        {{.uri = "/api/password/show", .method = HTTP_POST, .handler = auth_password_show}, WEB_OPEN},
        {{.uri = "/api/password/reset", .method = HTTP_POST, .handler = auth_password_reset}, WEB_OPEN},
        {{.uri = "/api/password/reset", .method = HTTP_DELETE, .handler = auth_password_reset}, WEB_OPEN},
        {{.uri = "/api/login/request", .method = HTTP_POST, .handler = auth_login_request}, WEB_OPEN},
        {{.uri = "/api/setup/claim", .method = HTTP_POST, .handler = auth_setup_claim}, WEB_OPEN},
        {{.uri = "/api/login/request/*", .method = HTTP_GET, .handler = auth_login_request_id}, WEB_OPEN},
        {{.uri = "/api/login/request/*", .method = HTTP_DELETE, .handler = auth_login_request_id}, WEB_OPEN},
        {{.uri = "/api/profile", .method = HTTP_GET, .handler = get_profile}, WEB_FULL},
        {{.uri = "/api/metrics", .method = HTTP_GET, .handler = get_metrics}, WEB_CONTROL},
        {{.uri = "/api/frontend", .method = HTTP_GET, .handler = get_frontend}, WEB_CONTROL},
        {{.uri = "/api/companion/found", .method = HTTP_GET, .handler = get_companions}, WEB_FULL},
#if !CONFIG_IDF_TARGET_LINUX
        {{.uri = "/api/cert", .method = HTTP_GET, .handler = get_cert}, WEB_OPEN},
        {{.uri = "/api/tls", .method = HTTP_PUT, .handler = put_tls}, WEB_ADMIN},
        {{.uri = "/api/tls", .method = HTTP_DELETE, .handler = delete_tls}, WEB_FULL},
#endif
        {{.uri = "/api/input/log", .method = HTTP_GET, .handler = get_input_log}, WEB_CONTROL},
        {{.uri = "/api/events", .method = HTTP_GET, .handler = get_events}, WEB_CONTROL},
        {{.uri = "/api/sources/retry", .method = HTTP_POST, .handler = post_source_retry}, WEB_CONTROL},
        {{.uri = "/api/sources/probe", .method = HTTP_POST, .handler = post_source_probe}, WEB_FULL},
        {{.uri = "/api/ota/inspect", .method = HTTP_POST, .handler = post_ota_inspect}, WEB_FULL},
        {{.uri = "/api/setup", .method = HTTP_PUT, .handler = put_setup}, WEB_FULL},
        {{.uri = "/api/update/check", .method = HTTP_GET, .handler = get_update_check}, WEB_FULL},
        {{.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = get_wifi_scan}, WEB_FULL},
        {{.uri = "/api/wifi/join", .method = HTTP_POST, .handler = post_wifi_join}, WEB_FULL},
        {{.uri = "/api/frontend", .method = HTTP_PUT, .handler = put_frontend}, WEB_FULL},
        {{.uri = "/api/frontend", .method = HTTP_DELETE, .handler = delete_frontend}, WEB_FULL},
        {{.uri = "/api/coredump", .method = HTTP_GET, .handler = get_coredump},
         WEB_ADMIN}, /* stacks can hold the password */
        {{.uri = "/api/boot", .method = HTTP_GET, .handler = get_boot}, WEB_FULL},
        {{.uri = "/api/coredump", .method = HTTP_DELETE, .handler = delete_coredump}, WEB_FULL},
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
        for (size_t k = 0; k < sizeof(deck_routes) / sizeof(deck_routes[0]); k++) {
            web_routes[i].deck |= list[i].uri.handler == deck_routes[k];
        }
        if (!list[i].uri.is_websocket) {
            web_routes[i].handler = list[i].uri.handler;
            web_uris[i].handler = timed;
            web_uris[i].user_ctx = &web_routes[i];
        }
        httpd_register_uri_handler(server, &web_uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, wrong_method);
    ESP_RETURN_ON_ERROR(events_start(state, api_screen_json), TAG, "web socket");
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
