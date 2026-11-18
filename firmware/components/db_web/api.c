#include "api.h"
#include "auth.h"
#include "events.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>

#include "cJSON.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_json.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_sat.h"
#include "db_satellite.h"
#include "db_screen.h"
#include "db_ui.h"
#include "db_version.h"
#include "db_web.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "web.h"
#if !CONFIG_IDF_TARGET_LINUX
#include "esp_core_dump.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#endif

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192

static db_device_info_t dev; /* too big for a stack; handlers run one at a time (web_lock) */
static const char *TAG = "api";

size_t (*api_status_json)(char *buf, size_t cap);

static int fail(char *reply, size_t cap, int status, const char *msg)
{
    const char *code = db_web_error_code(msg);
    if (code) {
        snprintf(reply, cap, "{\"ok\":false,\"error\":\"%s\",\"code\":\"%s\"}", msg, code);
    } else {
        snprintf(reply, cap, "{\"ok\":false,\"error\":\"%s\"}", msg);
    }
    return status;
}

/* The fields copied into `live` below apply at once; a change of any other one restarts the bridge. */
static int config_edit(const char *body, char *reply, size_t cap)
{
    static db_config_t old; /* two of them would crowd the caller's stack */
    static db_config_t cfg;
    db_config_copy(&old);
    cfg = old;
    char err[96];
    if (db_config_from_json(&cfg, body, err, sizeof(err)) != ESP_OK) {
        return fail(reply, cap, 400, err);
    }
    if (cfg.https_only && !old.https_only && !web_tls_running()) {
        return fail(reply, cap, 409, "HTTPS is not running, so plain HTTP must stay");
    }
    if (db_config_save(&cfg) != ESP_OK) {
        return fail(reply, cap, 500, "saving failed");
    }
    if (strcmp(cfg.admin_pass, old.admin_pass) != 0) {
        auth_claim_close();
        auth_end_sessions();
    }
    if (cfg.setup_done && !old.setup_done) {
        auth_end_sessions(); /* a session from the first-browser setup must not outlive it */
    }
    db_config_t live = cfg;
    live.brightness = old.brightness;
    live.controller = old.controller;
    live.setup_done = old.setup_done;
    memcpy(live.servers_first, old.servers_first, sizeof(old.servers_first));
    memcpy(live.servers_more, old.servers_more, sizeof(old.servers_more));
    memcpy(live.host, old.host, sizeof(old.host));
    memcpy(live.satellite, old.satellite, sizeof(old.satellite));
    memcpy(live.server_pins, old.server_pins, sizeof(old.server_pins));
    memcpy(live.satellite_pin, old.satellite_pin, sizeof(old.satellite_pin));
    live.dock_paired_only = old.dock_paired_only;
    live.qr_login_off = old.qr_login_off;
    live.satellite_auto_off = old.satellite_auto_off;
    live.https_only = old.https_only;
    memcpy(live.cors_origins, old.cors_origins, sizeof(old.cors_origins));
    memcpy(live.menu_chord, old.menu_chord, sizeof(old.menu_chord));
    memcpy(live.lang, old.lang, sizeof(old.lang));
    live.menu_hold_ms = old.menu_hold_ms;
    live.max_decks = old.max_decks;
    live.screen_off_s = old.screen_off_s;
    memcpy(live.wifi2_ssid, old.wifi2_ssid, sizeof(old.wifi2_ssid));
    memcpy(live.wifi2_pass, old.wifi2_pass, sizeof(old.wifi2_pass));
    bool restart = memcmp(&live, &old, sizeof(old)) != 0;
    if (!restart &&
        (strcmp(cfg.wifi2_ssid, old.wifi2_ssid) != 0 || strcmp(cfg.wifi2_pass, old.wifi2_pass) != 0)) {
        db_net_wifi_backup_changed();
    }
    if (cfg.screen_off_s != old.screen_off_s) {
        db_deck_screen_off(false);
    }
    for (int d = 0; cfg.brightness != old.brightness && d < DB_MAX_DECKS; d++) {
        db_deck_set_brightness(d, cfg.brightness);
    }
    if (!restart) {
        snprintf(reply, cap, "{\"ok\":true,\"restart\":false}");
        return 200;
    }
    /* where the page finds the bridge after the restart: a fixed LAN address as saved, the others as they are
     */
    db_net_status_t n;
    db_net_status(&n);
    const char *lan = !cfg.lan.dhcp && cfg.lan.ip[0] ? cfg.lan.ip : (n.lan_up ? n.lan_ip : "");
    const char *wlan =
        cfg.wifi_ssid[0] && strcmp(cfg.wifi_ssid, old.wifi_ssid) == 0 && n.wlan_up ? n.wlan_ip : "";
    const char *scheme = cfg.https_only ? "https" : "http";
    db_json_t j;
    db_json_begin(&j, reply, cap);
    db_json_obj_open(&j, NULL);
    db_json_bool(&j, "ok", true);
    db_json_bool(&j, "restart", true);
    db_json_arr_open(&j, "reach");
    char url[96];
    const char *hosts[] = {lan, wlan};
    for (int i = 0; i < 2; i++) {
        if (hosts[i][0]) {
            snprintf(url, sizeof(url), "%s://%s", scheme, hosts[i]);
            db_json_str(&j, NULL, url);
        }
    }
    snprintf(url, sizeof(url), "%s://%s.local", scheme, cfg.name);
    db_json_str(&j, NULL, url);
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    db_json_end(&j);
    ESP_LOGW(TAG, "config changed, restarting");
    restart_soon();
    return 200;
}

int api_config(const char *body, char *reply, size_t cap)
{
    db_config_edit_begin();
    int code = config_edit(body, reply, cap);
    db_config_edit_end();
    return code;
}

int db_web_config(const char *json, char *reply, size_t cap)
{
    if (!web_lock_for(10000)) {
        return fail(reply, cap, 503, "busy");
    }
    int code = api_config(json, reply, cap);
    web_unlock();
    return code;
}

static int json_int(const cJSON *root, const char *key, int fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

static bool in_touch_area(const db_device_info_t *dev, int x, int y)
{
    for (uint8_t t = 0; t < dev->touch_count; t++) {
        db_rect_t r = dev->touch[t].rect;
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) {
            return true;
        }
    }
    return false;
}

static const char *parse_touch(const cJSON *root, const char *kind, const db_device_info_t *dev,
                               db_input_t *ev)
{
    db_input_kind_t k = strcmp(kind, "tap") == 0     ? DB_INPUT_TOUCH_TAP
                        : strcmp(kind, "long") == 0  ? DB_INPUT_TOUCH_LONG
                        : strcmp(kind, "swipe") == 0 ? DB_INPUT_TOUCH_SWIPE
                                                     : 0;
    if (!k) {
        return "touch: tap, long or swipe expected";
    }
    int x = json_int(root, "x", -1);
    int y = json_int(root, "y", -1);
    int x2 = json_int(root, "x2", -1);
    int y2 = json_int(root, "y2", -1);
    if (!in_touch_area(dev, x, y) || (k == DB_INPUT_TOUCH_SWIPE && !in_touch_area(dev, x2, y2))) {
        return "x and y (and x2, y2 for a swipe) must lie on the touch strip";
    }
    *ev = (db_input_t){.kind = k, .x = (uint16_t)x, .y = (uint16_t)y};
    if (k == DB_INPUT_TOUCH_SWIPE) {
        ev->x2 = (uint16_t)x2;
        ev->y2 = (uint16_t)y2;
    }
    return NULL;
}

/* Fills `ev` (and `then` for a press: down, then up). Returns an error text or NULL. */
static const char *read_input(const cJSON *root, const db_device_info_t *dev, db_input_t *ev,
                              db_input_t *then)
{
    const cJSON *touch = cJSON_GetObjectItemCaseSensitive(root, "touch");
    if (cJSON_IsString(touch)) {
        return parse_touch(root, touch->valuestring, dev, ev);
    }
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    const char *a = cJSON_IsString(action) ? action->valuestring : "press";
    bool key = cJSON_GetObjectItemCaseSensitive(root, "key") != NULL;
    if (!key && !cJSON_GetObjectItemCaseSensitive(root, "encoder")) {
        return "key, encoder or touch expected";
    }
    int index = json_int(root, key ? "key" : "encoder", -1);
    if (index < 0 || index >= (key ? dev->key_count : dev->encoder_count)) {
        return key ? "no such key" : "no such encoder";
    }
    db_input_kind_t down = key ? DB_INPUT_KEY_DOWN : DB_INPUT_ENCODER_DOWN;
    *ev = (db_input_t){.kind = down, .index = (uint8_t)index};
    if (strcmp(a, "turn") == 0 && !key) {
        int ticks = json_int(root, "ticks", 0);
        if (ticks == 0 || ticks < -127 || ticks > 127) {
            return "ticks: -127 to 127, not 0";
        }
        ev->kind = DB_INPUT_ENCODER_TURN;
        ev->value = (int16_t)ticks;
    } else if (strcmp(a, "up") == 0) {
        ev->kind = (db_input_kind_t)(down + 1);
    } else if (strcmp(a, "press") == 0) {
        *then = (db_input_t){.kind = (db_input_kind_t)(down + 1), .index = (uint8_t)index};
    } else if (strcmp(a, "down") != 0) {
        return key ? "action: press, down or up expected" : "action: press, down, up or turn expected";
    }
    return NULL;
}

int api_input(int deck, const char *body, char *reply, size_t cap)
{
    cJSON *root = cJSON_Parse(body);
    const cJSON *serial = cJSON_GetObjectItemCaseSensitive(root, "deck");
    if (cJSON_IsString(serial)) {
        deck = db_deck_find(serial->valuestring);
        if (deck < 0) {
            cJSON_Delete(root);
            return fail(reply, cap, 404, "no such deck");
        }
    }
    deck = deck < 0 ? db_deck_first() : deck;
    if (deck < 0 || !db_deck_info(deck, &dev)) {
        cJSON_Delete(root);
        return fail(reply, cap, 409, "no deck attached");
    }
    db_input_t ev;
    db_input_t then = {.kind = 0};
    const char *err = root ? read_input(root, &dev, &ev, &then) : "invalid JSON";
    /* true: a person at the web UI. A tool names the host that may get it, and the deck task checks per press
     */
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "real");
    bool real = cJSON_IsTrue(r) || cJSON_IsString(r);
    uint32_t only_ip = cJSON_IsString(r) ? inet_addr(r->valuestring) : 0;
    if (cJSON_IsString(r) && (only_ip == INADDR_NONE || !only_ip)) {
        err = "real: true or the IPv4 address of the host that draws";
    }
    cJSON_Delete(root);
    if (err) {
        return fail(reply, cap, 400, err);
    }
    /* a press is down and up or nothing: a lone down would leave the key held */
    if (then.kind && db_deck_input_space() < 2) {
        return fail(reply, cap, 503, "input queue full");
    }
    esp_err_t sent = db_deck_simulate(deck, &ev, real, only_ip);
    if (sent == ESP_OK && then.kind) { /* the down is in: the up must follow, or the key stays held */
        for (int i = 0; (sent = db_deck_simulate(deck, &then, real, only_ip)) == ESP_ERR_TIMEOUT && i < 20;
             i++) {
        }
    }
    if (sent == ESP_ERR_INVALID_STATE) {
        return fail(reply, cap, 409, "no deck attached");
    }
    if (sent != ESP_OK) {
        return fail(reply, cap, 503, "input queue full");
    }
    snprintf(reply, cap, "{\"ok\":true}");
    return 200;
}

esp_err_t get_status(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(STATUS_JSON_MAX, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    int n = snprintf(buf, STATUS_JSON_MAX, "{\"via_ap\":%s", auth_via_ap(req) ? "true" : "false");
    esp_err_t err;
    if (api_status_json(buf + n, STATUS_JSON_MAX - n)) {
        buf[n] = ','; /* in place of the status's own '{' */
        err = send_json(req, buf);
    } else {
        err = send_error(req, "500 Internal Server Error", "too large");
    }
    heap_caps_free(buf);
    return err;
}

static size_t config_json(char *buf, size_t cap)
{
    return db_config_to_json(db_config(), false, buf, cap);
}

static size_t backup_json(char *buf, size_t cap)
{
    return db_config_to_json(db_config(), true, buf, cap);
}

/* quotes in the WLAN name or password grow the JSON past JSON_MAX */
esp_err_t get_config(httpd_req_t *req)
{
    return send_built(req, JSON_MAX * 2, config_json);
}

/* GET /api/events: the timeline of the last DB_EVENTS events, oldest first, earlier boots included */
static size_t events_json(char *buf, size_t cap)
{
    db_event_t *ev = heap_caps_malloc(DB_EVENTS * sizeof(*ev), MALLOC_CAP_SPIRAM);
    size_t n = ev ? db_log_events(ev, DB_EVENTS) : 0;
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    for (size_t i = 0; i < n; i++) {
        db_json_obj_open(&j, NULL);
        db_json_int(&j, "since_s", ev[i].at_us / 1000000); /* after the start of its boot */
        db_json_int(&j, "boot", ev[i].boot);
        if (ev[i].unix_s) {
            db_json_int(&j, "at", ev[i].unix_s);
        } else {
            db_json_null(&j, "at");
        }
        db_json_str(&j, "kind", ev[i].kind);
        db_json_str(&j, "text", ev[i].text);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    free(ev);
    return db_json_end(&j);
}

esp_err_t get_events(httpd_req_t *req)
{
    return send_built(req, DB_EVENTS * 160, events_json);
}

/* GET /api/backup?secrets=1: the settings with the admin password, the WLAN password and the server token,
 * for a restore through PUT /api/config. Without it the same as GET /api/config. API tokens (only their
 * hashes are kept), paired hosts and the TLS key stay out: create new tokens, pair again, upload the key
 * again. */
esp_err_t get_backup(httpd_req_t *req)
{
    char query[32] = "";
    char v[4] = "";
    bool secrets = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                   httpd_query_key_value(query, "secrets", v, sizeof(v)) == ESP_OK && strcmp(v, "1") == 0;
    return send_built(req, JSON_MAX * 2, secrets ? backup_json : config_json);
}

static const char *status_line(int code)
{
    switch (code) {
    case 200:
        return "200 OK";
    case 423:
        return "423 Locked";
    case 400:
        return "400 Bad Request";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
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
    return send_json(req, reply);
}

/* A JSON body handed to one of the api_* functions shared with the web socket. */
static esp_err_t body_api(httpd_req_t *req, int (*api)(const char *body, char *reply, size_t cap))
{
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    char reply[160];
    return reply_with(req, api(body, reply, sizeof(reply)), reply);
}

/* A token, even with full rights, never changes the password, nor reopens setup: the deck then shows a
 * one-time login QR, which /api/screen.jpg would hand to the token. Decided on the parsed keys: cJSON decodes
 * \u escapes. */
int api_config_token(const char *body, char *reply, size_t cap)
{
    static const char *const password_only[] = {"admin_pass", "setup_done", "qr_login"};
    cJSON *root = cJSON_Parse(body);
    bool refused = false;
    for (size_t i = 0; root && i < sizeof(password_only) / sizeof(*password_only); i++) {
        refused |= cJSON_GetObjectItemCaseSensitive(root, password_only[i]) != NULL;
    }
    cJSON_Delete(root);
    return refused ? fail(reply, cap, 403, "needs the password, not a token") : api_config(body, reply, cap);
}

/* The reply carries the stored settings, so a page shows what the bridge keeps, not what it sent. */
esp_err_t put_config(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    enum { REPLY_MAX = JSON_MAX * 2 + 64 };
    static char *reply; /* in PSRAM, made once; handlers run under the handler lock */
    if (!reply && !(reply = heap_caps_malloc(REPLY_MAX, MALLOC_CAP_SPIRAM))) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    bool token = auth_by_token(req);
    static char old_pass[sizeof(db_config()->admin_pass)];
    strlcpy(old_pass, db_config()->admin_pass, sizeof(old_pass));
    bool was_done = db_config()->setup_done;
    int code = (token ? api_config_token : api_config)(body, reply, 320);
    /* a new password or the end of setup ends every session, also this one: the wizard's next step would get
     * a 401 */
    if (code == 200 && !token &&
        (strcmp(old_pass, db_config()->admin_pass) != 0 || (!was_done && db_config()->setup_done))) {
        auth_session_cookie(req);
    }
    mbedtls_platform_zeroize(old_pass, sizeof(old_pass));
    if (code == 200) {
        size_t n = strlen(reply) - 1; /* over the closing brace */
        n += snprintf(reply + n, REPLY_MAX - n, ",\"config\":");
        size_t len = db_config_to_json(db_config(), false, reply + n, REPLY_MAX - n - 1);
        n += len ? len : (size_t)snprintf(reply + n, REPLY_MAX - n, "null");
        snprintf(reply + n, REPLY_MAX - n, "}");
    }
    return reply_with(req, code, reply);
}

/* Companion's Discover, "Setup" on a found Satellite: an unauthenticated POST of {"host", "port"}, as the
 * official Satellite takes it. A bridge still in setup takes it at once; a set-up one asks on the deck, so
 * nobody on the network takes it over without a tap there. */
esp_err_t post_companion_setup(httpd_req_t *req)
{
    static int64_t last_us;
    int64_t now = esp_timer_get_time();
    if (last_us && now - last_us < 2000000) {
        return send_error(req, "429 Too Many Requests", "one Companion setup per 2 s");
    }
    last_us = now;
    if (!auth_host_is_local(req)) {
        return send_error(req, "403 Forbidden", "address the bridge by its IP or .local name");
    }
    /* a web page can POST only text/plain and form types without a CORS preflight, which we refuse */
    char type[40] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type));
    if (strncasecmp(type, "application/json", 16) != 0) {
        return send_error(req, "415 Unsupported Media Type", "application/json expected");
    }
    char body[192];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(root, "host");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "port");
    char addr[sizeof(db_config()->satellite)] = "";
    char h_copy[48] = "";
    int port = cJSON_IsNumber(p) ? p->valueint : 0;
    if (cJSON_IsString(h) && h->valuestring[0] && strlen(h->valuestring) < 48 &&
        db_config_ntp_ok(h->valuestring) && !strchr(h->valuestring, ':') && port > 0 && port < 65536) {
        snprintf(addr, sizeof(addr), "%s:%d", h->valuestring, port);
        strcpy(h_copy, h->valuestring);
    }
    cJSON_Delete(root);
    if (!addr[0]) {
        return send_error(req, "400 Bad Request", "host and port expected");
    }
    const db_config_t *c = db_config();
    bool same = strcmp(c->satellite, addr) == 0 || (port == 16622 && strcmp(c->satellite, h_copy) == 0);
    if (c->controller == DB_CTRL_COMPANION && strcmp(c->host, "satellite") == 0 && same) {
        return send_json(req, "{\"ok\":true}");
    }
    /* during setup it is the source the web wizard would pick; later only a tap on the deck hands it over */
    if (c->setup_done) {
        const char *why = db_ui_ask_companion(addr, 60);
        if (why) {
            return send_error(req, "409 Conflict", why);
        }
        db_log_event(TAG, "companion", "Companion %s asks for the deck", addr);
        return send_json(req, "{\"ok\":true,\"confirm\":\"deck\"}");
    }
    db_config_t n;
    db_config_edit_begin();
    db_config_copy(&n);
    snprintf(n.satellite, sizeof(n.satellite), "%s", addr);
    n.satellite_pin[0] = '\0'; /* a pin of an old wss:// address would be applied to the next one */
    n.satellite_auto_off = false;
    n.controller = DB_CTRL_COMPANION;
    snprintf(n.host, sizeof(n.host), "satellite");
    esp_err_t err = db_config_save(&n);
    db_config_edit_end();
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    db_log_event(TAG, "companion", "Companion %s set up over Satellite by its Discover", addr);
    return send_json(req, "{\"ok\":true}");
}

int web_deck;

/* `?deck=<serial>` picks a deck; without it the first. A deck that is not attached gets a 404. While the deck
 * shows a login code or a password, its pictures would hand a token a password session. */
bool web_deck_refused(httpd_req_t *req)
{
    if (auth_by_token(req) && db_ui_login_shown()) {
        send_error(req, "403 Forbidden", "the deck shows a login: for the password only");
        return true;
    }
    char query[64];
    char want[32];
    int first = db_deck_first();
    web_deck = first >= 0 ? first : 0; /* none attached: the routes answer for slot 0, which has none */
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "deck", want, sizeof(want)) != ESP_OK) {
        return false;
    }
    web_deck = db_deck_find(want);
    if (web_deck >= 0) {
        return false;
    }
    send_error(req, "404 Not Found", "no such deck");
    return true;
}

static int input_here(const char *body, char *reply, size_t cap)
{
    return api_input(web_deck, body, reply, cap);
}

esp_err_t post_input(httpd_req_t *req)
{
    return body_api(req, input_here);
}

esp_err_t post_menu(httpd_req_t *req)
{
    esp_err_t err = db_ui_open_menu(web_deck);
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict",
                          err == ESP_ERR_NOT_ALLOWED ? "Companion locked this deck" : "no deck connected");
    }
    return send_json(req, "{\"ok\":true}");
}

esp_err_t delete_menu(httpd_req_t *req)
{
    db_ui_close_menu();
    return send_json(req, "{\"ok\":true}");
}

/* {"on": false} turns every deck dark now, {"on": true} lights them; neither swallows a press. */
esp_err_t post_screen(httpd_req_t *req)
{
    char body[32];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    bool ok = cJSON_IsBool(on);
    if (ok) {
        db_deck_screen_off(!cJSON_IsTrue(on));
    }
    cJSON_Delete(root);
    return ok ? send_json(req, "{\"ok\":true}")
              : send_error(req, "400 Bad Request", "on: true or false expected");
}

/* {"accept": true|false} answers a request, {"pairing": seconds} opens pairing (0 closes it), {"forget":
 * true} drops every paired host, {"forget": "IP"} one. */
esp_err_t post_dock(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *accept = cJSON_GetObjectItemCaseSensitive(root, "accept");
    const cJSON *pairing = cJSON_GetObjectItemCaseSensitive(root, "pairing");
    const cJSON *forget = cJSON_GetObjectItemCaseSensitive(root, "forget");
    bool ok = true;
    esp_err_t saved = ESP_OK;
    const cJSON *ip = cJSON_GetObjectItemCaseSensitive(root, "ip");
    char asking[16];
    if (cJSON_IsBool(accept)) {
        /* the answer names the host: a second click must not answer a request that came in between */
        if (!cJSON_IsString(ip) || !db_dock_request(asking) || strcmp(asking, ip->valuestring) != 0) {
            cJSON_Delete(root);
            return send_error(req, "409 Conflict", "accept needs the ip of the host that asks now");
        }
        saved = db_dock_answer(cJSON_IsTrue(accept));
    } else if (cJSON_IsNumber(pairing) && pairing->valueint >= 0 && pairing->valueint <= 3600) {
        db_dock_pairing(pairing->valueint);
    } else if (cJSON_IsTrue(forget) || cJSON_IsString(forget)) {
        saved = db_dock_forget(cJSON_IsString(forget) ? forget->valuestring : NULL);
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    if (saved != ESP_OK) {
        return send_error(req, "500 Internal Server Error",
                          "not saved: the list is as before after a restart");
    }
    return ok ? send_json(req, "{\"ok\":true}")
              : send_error(req, "400 Bad Request", "accept, pairing (0 to 3600 s) or forget expected");
}

/* DELETE /api/pairing/denied/<ip>: off the list of refused hosts; it is asked about again when it comes back
 */
esp_err_t delete_denied(httpd_req_t *req)
{
    uint32_t ip = inet_addr(req->uri + strlen("/api/pairing/denied/"));
    if (ip == INADDR_NONE || !db_dock_undeny(ip)) {
        return send_error(req, "404 Not Found", "not on the list");
    }
    return send_json(req, "{\"ok\":true}");
}

esp_err_t post_deck_reconnect(httpd_req_t *req)
{
    char query[128];
    char off[8];
    uint32_t off_ms = 500;
    esp_err_t got = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (got == ESP_OK) {
        got = httpd_query_key_value(query, "off_s", off, sizeof(off));
    }
    if (got != ESP_ERR_NOT_FOUND) { /* there, or cut off */
        char *end = off;
        long off_s = got == ESP_OK && off[0] >= '0' && off[0] <= '9' ? strtol(off, &end, 10) : 0;
        if (*end || off_s < 1 || off_s > 600) {
            return send_error(req, "400 Bad Request", "off_s is 1 to 600");
        }
        off_ms = off_s * 1000;
    }
    if (!db_deck_reconnect(off_ms)) {
        return send_error(req, "409 Conflict", "the deck is already reconnecting");
    }
    return send_json(req, "{\"ok\":true}");
}

esp_err_t post_deck_stall(httpd_req_t *req)
{
    if (db_deck_stall(web_deck) != ESP_OK) {
        return send_error(req, "409 Conflict", "no deck");
    }
    return send_json(req, "{\"ok\":true}");
}

/* the bridge failed, not the file: the client should not blame its upload */
static bool device_fault(esp_err_t err)
{
    return err == ESP_ERR_NO_MEM || (err >= ESP_ERR_FLASH_BASE && err < ESP_ERR_FLASH_BASE + 0x100);
}

/* POST /api/ota/inspect with the first 144 bytes of a .dbfw file: what it is, and whether an upload would
 * pass */
esp_err_t post_ota_inspect(httpd_req_t *req)
{
    static const char *const targets[] = {"", "p4", "c6", "web", "bootloader", "ptable"};
    uint8_t h[145];
    if (read_body(req, (char *)h, sizeof(h)) != 144) {
        return send_error(req, "400 Bad Request", "file too short: send the first 144 bytes of the file");
    }
    const char *why = db_ota_inspect(h);
    char version[33], sha[65], body[512]; /* a version of control characters escapes to 6 bytes each */
    snprintf(version, sizeof(version), "%.32s", (const char *)h + 12);
    db_hex(sha, h + 44, 32);
    uint32_t len;
    memcpy(&len, h + 8, 4);
    db_json_t j;
    db_json_begin(&j, body, sizeof(body));
    db_json_obj_open(&j, NULL);
    db_json_bool(&j, "ok", !why);
    db_json_str(&j, "error", why ? why : "");
    if (db_web_error_code(why)) {
        db_json_str(&j, "code", db_web_error_code(why));
    }
    db_json_str(&j, "target", h[5] < 6 ? targets[h[5]] : "");
    if (memcmp(h, "DBFW", 4) == 0) { /* junk has no version */
        db_json_str(&j, "version", version);
    }
    db_json_int(&j, "length", len);
    db_json_str(&j, "sha256", sha);
    db_json_obj_close(&j);
    db_json_end(&j);
    return send_json(req, body);
}

/* `?slot=N` writes into that app slot, `?boot=0` keeps the running firmware. */
esp_err_t post_ota(httpd_req_t *req)
{
    char q[32] = "", v[8];
    int slot = -1;
    bool boot = true;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "slot", v, sizeof(v)) == ESP_OK) {
            slot = atoi(v) - 1; /* the API counts slots from 1 */
            if (slot < 0) {     /* an old client's slot 0 must not mean "the next slot" */
                return send_error(req, "400 Bad Request", "slot: 1 or more");
            }
        }
        if (httpd_query_key_value(q, "boot", v, sizeof(v)) == ESP_OK) {
            boot = strcmp(v, "0") != 0;
        }
    }
    esp_err_t begun = db_ota_begin(req->content_len, slot, boot);
    if (begun == ESP_ERR_INVALID_SIZE) {
        return send_error(req, "400 Bad Request", "file too short for a .dbfw file");
    }
    if (begun == ESP_ERR_INVALID_ARG) {
        return send_error(req, "413 Payload Too Large", "file larger than a firmware slot");
    }
    if (begun != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running");
    }
    /* Flash writes stall the network between chunks, so the whole file comes in first when PSRAM allows:
     * 1.5 MB took 28 s streamed. */
    size_t cap = req->content_len;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        cap = CHUNK;
        buf = heap_caps_malloc(cap, MALLOC_CAP_INTERNAL);
    }
    if (!buf) {
        db_ota_abort("memory");
        return send_error(req, "500 Internal Server Error", "no memory for the upload");
    }
    size_t left = req->content_len, have = 0;
    esp_err_t err = ESP_OK;
    int timeouts = 0;
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(req, (char *)buf + have, left < cap - have ? left : cap - have);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue; /* a vanished client must not hold the update lock forever */
        }
        if (n <= 0) {
            err = ESP_FAIL;
            db_ota_abort("upload interrupted");
            break;
        }
        timeouts = 0;
        have += (size_t)n;
        left -= (size_t)n;
        if (have == cap || left == 0) {
            err = db_ota_write(buf, have);
            have = 0;
        }
    }
    heap_caps_free(buf);
    if (err == ESP_OK) {
        err = db_ota_end();
    }
    /* db_ota_write and db_ota_end end the update themselves: aborting again here could end one started since
     */
    if (err == DB_OTA_ERR_HALF_WRITTEN) {
        return send_error(req, "500 Internal Server Error",
                          "written incompletely: do not power off, write it again or flash over USB");
    }
    if (err != ESP_OK) {
        const char *why = db_ota_refusal();
        return send_error(req, device_fault(err) ? "500 Internal Server Error" : "400 Bad Request",
                          why ? why : esp_err_to_name(err));
    }
    return send_json(req, db_ota_restarts() ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true}");
}

static const char *slot_refusal_text(esp_err_t err)
{
    return err == ESP_ERR_INVALID_VERSION ? "slot below the security level"
           : err == ESP_ERR_INVALID_ARG   ? "no security level noted for the slot"
           : err == ESP_ERR_INVALID_CRC   ? "the image in the slot is marked invalid"
           : err == ESP_ERR_INVALID_STATE ? "the running firmware has not confirmed itself yet"
           : err == ESP_ERR_INVALID_SIZE  ? "no firmware in the slot"
           : err == ESP_ERR_NOT_FOUND     ? "no such slot"
                                          : esp_err_to_name(err);
}

static size_t slots_json(char *buf, size_t cap)
{
    db_ota_slot_t slots[16];
    int n = db_ota_slots(slots, 16);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    for (int i = 0; i < n; i++) {
        db_json_obj_open(&j, NULL);
        db_json_int(&j, "slot", i + 1);
        db_json_str(&j, "label", slots[i].label);
        db_json_str(&j, "state", slots[i].state);
        if (slots[i].version[0]) {
            db_json_str(&j, "version", slots[i].version);
            db_json_str(&j, "built", slots[i].built);
        }
        db_json_bool(&j, "running", slots[i].running);
        db_json_bool(&j, "next", slots[i].next);
        if (slots[i].level >= 0) {
            db_json_int(&j, "level", slots[i].level);
        }
        esp_err_t refusal = slots[i].running ? ESP_OK : db_ota_slot_refusal(i);
        db_json_bool(&j, "bootable", !slots[i].running && refusal == ESP_OK);
        if (refusal != ESP_OK) {
            db_json_str(&j, "why_not", slot_refusal_text(refusal));
        }
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    return db_json_end(&j);
}

esp_err_t get_slots(httpd_req_t *req)
{
    return send_built(req, 2048, slots_json);
}

static size_t frontend_json(char *buf, size_t cap)
{
    db_ota_web_t web;
    bool own = db_ota_web(&web);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_int(&j, "api", DB_WEB_API);
    db_json_str(&j, "source", own ? "partition" : "builtin");
    if (!own && db_ota_web_rejected() != ESP_OK) {
        db_json_str(&j, "rejected", esp_err_to_name(db_ota_web_rejected()));
    }
    if (own) {
        db_json_str(&j, "version", web.version);
        db_json_int(&j, "api_min", web.api_min);
        db_json_int(&j, "size", (int64_t)web.len);
    }
    db_json_obj_close(&j);
    return db_json_end(&j);
}

esp_err_t get_frontend(httpd_req_t *req)
{
    return send_built(req, 256, frontend_json);
}

static size_t companions_json(char *buf, size_t cap)
{
    db_net_found_t found[4];
    size_t n = db_net_companions(found, 4);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    for (size_t i = 0; i < n; i++) {
        db_json_obj_open(&j, NULL);
        db_json_str(&j, "name", found[i].name);
        db_json_str(&j, "host", found[i].host);
        db_json_int(&j, "port", found[i].port);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    return db_json_end(&j);
}

static size_t boot_json(char *buf, size_t cap)
{
    db_ota_boot_t b;
    db_ota_boot_info(&b);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_obj_open(&j, "bootloader");
    db_json_int(&j, "version", b.bootloader_version);
    db_json_str(&j, "idf", b.bootloader_idf);
    db_json_str(&j, "built", b.bootloader_built);
    db_json_str(&j, "sha256", b.bootloader_sha256);
    db_json_obj_close(&j);
    db_json_obj_open(&j, "ptable");
    db_json_int(&j, "partitions", b.partitions);
    db_json_str(&j, "md5", b.table_md5);
    db_json_str(&j, "sha256", b.table_sha256);
    db_json_obj_close(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static esp_err_t send_region(httpd_req_t *req, bool bootloader)
{
    size_t size = db_ota_boot_size(bootloader);
    char *chunk = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!size || !chunk) {
        heap_caps_free(chunk);
        return send_error(req, "500 Internal Server Error", "cannot read it");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = ESP_OK;
    for (size_t off = 0; off < size && err == ESP_OK; off += 4096) {
        size_t n = size - off < 4096 ? size - off : 4096;
        err = db_ota_boot_read(bootloader, off, chunk, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, chunk, (ssize_t)n);
        }
    }
    heap_caps_free(chunk);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

/* ?raw=bootloader or ?raw=ptable: the region as it is on the chip, a backup before writing a new one. */
esp_err_t get_boot(httpd_req_t *req)
{
    if (strstr(req->uri, "raw=")) {
        return send_region(req, strstr(req->uri, "raw=bootloader") != NULL);
    }
    return send_built(req, 512, boot_json);
}

esp_err_t get_companions(httpd_req_t *req)
{
    return send_built(req, 512, companions_json);
}

#define SCAN_MAX 40

static size_t scan_json(char *buf, size_t cap)
{
    db_net_ap_t *aps = calloc(SCAN_MAX, sizeof(*aps));
    int n = aps ? db_net_wifi_scan(aps, SCAN_MAX) : -1;
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    for (int i = 0; i < n; i++) {
        db_json_obj_open(&j, NULL);
        db_json_str(&j, "ssid", aps[i].ssid);
        db_json_str(&j, "bssid", aps[i].bssid);
        db_json_int(&j, "rssi", aps[i].rssi);
        db_json_int(&j, "channel", aps[i].channel);
        db_json_bool(&j, "open", aps[i].open);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    free(aps);
    return n < 0 ? 0 : db_json_end(&j);
}

static uint32_t log_since;

static size_t input_log_json(char *buf, size_t cap)
{
    return events_input_log(buf, cap, log_since);
}

static int setup_step = -1; /* -1: not read from NVS yet */

int db_web_setup_step(void)
{
    if (setup_step < 0) {
        uint8_t v = 0;
        nvs_handle_t h;
        if (nvs_open("web", NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, "step", &v);
            nvs_close(h);
        }
        setup_step = v;
    }
    return setup_step;
}

/* PUT /api/setup {"step": 0..6}: kept in NVS, because a network change on the way restarts the bridge */
esp_err_t put_setup(httpd_req_t *req)
{
    char body[32];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *step = cJSON_GetObjectItemCaseSensitive(root, "step");
    int n = cJSON_IsNumber(step) ? step->valueint : -1;
    cJSON_Delete(root);
    if (n < 0 || n > 6) {
        return send_error(req, "400 Bad Request", "step: 0 to 6");
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("web", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "step", (uint8_t)n);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    setup_step = n;
    db_ui_setup_step(n);
    return send_json(req, "{\"ok\":true}");
}

#define RELEASES "https://github.com/FusselTV/DeckConnector/releases.atom"
#define FEED_MAX (128 * 1024)
#define TAG_PATH "/releases/tag/"

/* The release feed as text, NULL with *why and *code set on failure; the caller frees it with heap_caps_free.
 */
static char *fetch(const char *url, const char **why, const char **code)
{
    *code = "unreachable";
#if CONFIG_IDF_TARGET_LINUX
    *why = "the simulator has no network";
    return NULL;
#else
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "DeckConnector",
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char *body = heap_caps_malloc(FEED_MAX, MALLOC_CAP_SPIRAM);
    int got = 0, n = 0, status = 0;
    *why = !c || !body ? "out of memory" : NULL;
    if (!*why && (esp_http_client_open(c, 0) != ESP_OK || esp_http_client_fetch_headers(c) < 0)) {
        *why = "GitHub not reachable";
    } else if (!*why && (status = esp_http_client_get_status_code(c)) != 200) {
        /* 404: a repository without releases, or a private one */
        *why = status == 404   ? "no releases yet"
               : status == 429 ? "GitHub limits checks, try again later"
                               : "GitHub not reachable";
        *code = status == 404 ? "no_releases" : status == 429 ? "rate_limited" : "unreachable";
    }
    while (!*why && got < FEED_MAX - 1 && (n = esp_http_client_read(c, body + got, FEED_MAX - 1 - got)) > 0) {
        got += n;
    }
    if (!*why && (n < 0 || !esp_http_client_is_complete_data_received(c))) {
        *why = "GitHub not reachable"; /* cut off: what came is no list */
    }
    if (c) {
        esp_http_client_cleanup(c);
    }
    if (*why) {
        heap_caps_free(body);
        return NULL;
    }
    body[got] = '\0';
    return body;
#endif
}

/* %XX in a query value, in place: esp_http_server leaves it encoded */
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        unsigned v;
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2]) &&
            sscanf(s + 1, "%2x", &v) == 1) {
            *o++ = (char)v;
            s += 2;
        } else {
            *o++ = *s == '+' ? ' ' : *s;
        }
    }
    *o = '\0';
}

static portMUX_TYPE check_mux = portMUX_INITIALIZER_UNLOCKED;
static struct { /* the last good answer, under check_mux */
    int64_t at;
    char feed[160];
    char reply[512];
} cache;

/* GET /api/update/check: the newest release in the repository's release feed, a prerelease only while one
 * runs. `url` (the release's .dbfw by the name the release workflow gives it, for POST /api/ota/url) only
 * when it is newer than the running firmware. `?feed=` takes another feed of the same format, for tests. Not
 * under the web lock: GitHub can take seconds. Good answers are kept for a minute. */
esp_err_t get_update_check(httpd_req_t *req)
{
    char q[192] = "", feed[160] = RELEASES;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        esp_err_t got = httpd_query_key_value(q, "feed", feed, sizeof(feed));
        if (got != ESP_OK && got != ESP_ERR_NOT_FOUND) {
            return send_error(req, "400 Bad Request", "feed: a URL of up to 159 characters");
        }
        url_decode(feed);
    }
    char reply[sizeof(cache.reply)];
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&check_mux);
    bool hit = cache.at && now - cache.at < 60 * 1000000LL && strcmp(feed, cache.feed) == 0;
    if (hit) {
        strcpy(reply, cache.reply);
    }
    portEXIT_CRITICAL(&check_mux);
    if (hit) {
        return send_json(req, reply);
    }
    const char *current = esp_app_get_description()->version;
    const char *why = NULL, *code = NULL;
    char *text = fetch(feed, &why, &code);
    /* every release links its page: "https://github.com/owner/repo/releases/tag/v1.2.3" */
    char best[40] = "", base[96] = "";
    bool pre_ok = db_version_pre(current);
    for (const char *p = text; p && (p = strstr(p, TAG_PATH)); p += strlen(TAG_PATH)) {
        const char *tag = p + strlen(TAG_PATH);
        size_t len = strcspn(tag, "\"<& ");
        char t[sizeof(best)];
        if (len == 0 || len >= sizeof(t)) {
            continue;
        }
        memcpy(t, tag, len);
        t[len] = '\0';
        if (db_version_cmp(t, "v0.0.0") < 0 || (!pre_ok && db_version_pre(t)) ||
            (best[0] && db_version_cmp(t, best) <= 0)) {
            continue;
        }
        const char *from = p;
        while (from > text && from[-1] != '"') {
            from--;
        }
        if ((size_t)(p - from) < sizeof(base)) {
            strcpy(best, t);
            snprintf(base, sizeof(base), "%.*s", (int)(p - from), from);
        }
    }
    heap_caps_free(text);
    db_json_t j;
    db_json_begin(&j, reply, sizeof(reply));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "current", current);
    if (!best[0]) {
        db_json_null(&j, "latest");
        db_json_str(&j, "error", why ? why : "no releases yet");
        db_json_str(&j, "code", why ? code : "no_releases");
    } else {
        char link[sizeof(base) + 2 * sizeof(best) + 48];
        db_json_str(&j, "latest", best);
        snprintf(link, sizeof(link), "%s" TAG_PATH "%s", base, best);
        db_json_str(&j, "notes", link);
        if (db_version_cmp(best, current) > 0) {
            snprintf(link, sizeof(link), "%s/releases/download/%s/deckconnector-%s.dbfw", base, best, best);
            db_json_str(&j, "url", link);
        }
    }
    db_json_obj_close(&j);
    if (!db_json_end(&j)) {
        return send_error(req, "500 Internal Server Error", "reply too long");
    }
    if (!why) { /* a failure is asked again at once */
        portENTER_CRITICAL(&check_mux);
        cache.at = now;
        strcpy(cache.feed, feed);
        strcpy(cache.reply, reply);
        portEXIT_CRITICAL(&check_mux);
    }
    return send_json(req, reply);
}

/* POST /api/sources/probe {"addr", "pin"}: is Companion's Satellite API there? Blocks for up to 6 s. */
esp_err_t post_source_probe(httpd_req_t *req)
{
    char body[200];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *addr = cJSON_GetObjectItemCaseSensitive(root, "addr");
    const cJSON *pin = cJSON_GetObjectItemCaseSensitive(root, "pin");
    if (!cJSON_IsString(addr) || !addr->valuestring[0] || strlen(addr->valuestring) >= 64) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "addr: host, host:port, or a ws:// or wss:// URL");
    }
    char version[24] = "", reply[160];
    const char *why = db_satellite_probe(addr->valuestring, cJSON_IsString(pin) ? pin->valuestring : "",
                                         version, sizeof(version));
    cJSON_Delete(root);
    db_json_t j;
    db_json_begin(&j, reply, sizeof(reply));
    db_json_obj_open(&j, NULL);
    db_json_bool(&j, "ok", !why);
    db_json_str(&j, "error", why ? why : "");
    if (db_web_error_code(why)) {
        db_json_str(&j, "code", db_web_error_code(why));
    }
    db_json_str(&j, "version", version);
    db_json_obj_close(&j);
    db_json_end(&j);
    return send_json(req, reply);
}

/* GET /api/input/log?since=<seq> */
esp_err_t get_input_log(httpd_req_t *req)
{
    char q[32], v[12];
    log_since = httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
                        httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK
                    ? (uint32_t)strtoul(v, NULL, 10)
                    : 0;
    return send_built(req, INPUT_LOG * 180 + 16,
                      input_log_json); /* runs under the handler lock: log_since stays put */
}

/* A server or Companion's Satellite address: connect now instead of after the wait. Dock hosts connect to us.
 */
esp_err_t post_source_retry(httpd_req_t *req)
{
    char body[160];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "host");
    bool found = false;
    if (cJSON_IsString(host) && host->valuestring[0]) {
        for (int i = 0; i < DB_MAX_SERVERS; i++) {
            if (strcmp(db_link_url(i), host->valuestring) == 0) {
                db_link_retry(i);
                found = true;
            }
        }
        if (strcmp(db_satellite_address(), host->valuestring) == 0) {
            db_satellite_retry();
            found = true;
        }
    }
    cJSON_Delete(root);
    return found ? send_json(req, "{\"ok\":true}")
                 : send_error(req, "404 Not Found", "no server or Satellite address with that host");
}

/* Takes 2 to 3 s: the radio listens on every channel. */
esp_err_t get_wifi_scan(httpd_req_t *req)
{
    return send_built(req, SCAN_MAX * 120 + 16, scan_json);
}

esp_err_t post_wifi_join(httpd_req_t *req)
{
    char body[256];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(root, "pass");
    const cJSON *bssid = cJSON_GetObjectItemCaseSensitive(root, "bssid");
    bool pass_ok = !pass || (cJSON_IsString(pass) && db_config_wifi_pass_ok(pass->valuestring));
    esp_err_t err = cJSON_IsString(ssid) && pass_ok && (!bssid || cJSON_IsString(bssid))
                        ? db_net_wifi_join(ssid->valuestring, pass ? pass->valuestring : "",
                                           bssid ? bssid->valuestring : "")
                        : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(
            req, "400 Bad Request",
            "ssid (1 to 32), pass (8 to 63, 64 hex digits or empty) and an optional bssid expected");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict",
                          err == ESP_ERR_INVALID_STATE ? "a test is running" : esp_err_to_name(err));
    }
    httpd_resp_set_status(req, "202 Accepted");
    return send_json(req, "{\"ok\":true,\"join\":\"testing\"}");
}

#define FRONTEND_MAX (1024 * 1024) /* the www partition */

esp_err_t put_frontend(httpd_req_t *req)
{
    size_t len = req->content_len;
    uint8_t *img = len && len <= FRONTEND_MAX ? heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : NULL;
    if (!img) {
        return send_error(req, len > FRONTEND_MAX ? "413 Content Too Large" : "400 Bad Request",
                          "a .dbfw file expected");
    }
    for (size_t have = 0; have < len;) {
        int n = httpd_req_recv(req, (char *)img + have, len - have);
        if (n <= 0) {
            heap_caps_free(img);
            return send_error(req, "400 Bad Request", "upload interrupted");
        }
        have += (size_t)n;
    }
    esp_err_t err = db_ota_web_store(img, len, DB_WEB_API);
    if (err != ESP_OK) {
        heap_caps_free(img);
        if (err == ESP_FAIL) {
            return send_error(req, "500 Internal Server Error",
                              "page not saved, the built-in one is used after a restart");
        }
        return send_error(req, device_fault(err) ? "500 Internal Server Error" : "400 Bad Request",
                          err == ESP_ERR_INVALID_VERSION ? "needs a newer firmware" : esp_err_to_name(err));
    }
    return send_built(req, 256, frontend_json);
}

esp_err_t delete_frontend(httpd_req_t *req)
{
    esp_err_t err = db_ota_web_erase();
    return err == ESP_OK ? send_built(req, 256, frontend_json)
                         : send_error(req, "500 Internal Server Error", esp_err_to_name(err));
}

/* POST /api/slots/<n>/boot */
esp_err_t post_slot_boot(httpd_req_t *req)
{
    int slot;
    char tail[8] = "";
    if (sscanf(req->uri, "/api/slots/%d/%7s", &slot, tail) != 2 || strcmp(tail, "boot") != 0) {
        return send_error(req, "404 Not Found", "use /api/slots/<n>/boot");
    }
    esp_err_t err = db_ota_boot_slot(slot - 1);
    if (err != ESP_OK) {
        return send_error(req, err == ESP_ERR_NOT_FOUND ? "404 Not Found" : "409 Conflict",
                          slot_refusal_text(err));
    }
    return send_json(req, "{\"ok\":true,\"restart\":true}");
}

/* The page on the setup AP closes it once the bridge is in the user's WLAN: the phone goes back there. The
 * answer goes out first, over the AP. */
esp_err_t delete_ap(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    db_net_ap_stop();
    return ESP_OK;
}

esp_err_t post_reboot(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    restart_soon();
    return ESP_OK;
}

esp_err_t post_reset(httpd_req_t *req)
{
    if (db_ui_factory_reset() != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "reset failed");
    }
    ESP_LOGW(TAG, "factory reset from the web page");
    send_json(req, "{\"ok\":true}");
    restart_soon();
    return ESP_OK;
}

esp_err_t get_log(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    char q[16], v[8];
    bool prev = httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
                httpd_query_key_value(q, "boot", v, sizeof(v)) == ESP_OK && strcmp(v, "prev") == 0;
    size_t n = prev ? db_log_prev(buf, 16 * 1024) : db_log_read(buf, 16 * 1024);
    if (prev && !n) {
        heap_caps_free(buf);
        return send_error(req, "404 Not Found", "no log from the boot before: it started from power off");
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, (ssize_t)n);
    heap_caps_free(buf);
    return err;
}

static void write_fields(db_json_t *j, db_rect_t r)
{
    db_json_int(j, "x", r.x);
    db_json_int(j, "y", r.y);
    db_json_int(j, "w", r.w);
    db_json_int(j, "h", r.h);
}

static void write_rect(db_json_t *j, db_rect_t r)
{
    db_json_obj_open(j, NULL);
    write_fields(j, r);
    db_json_obj_close(j);
}

static void write_geometry(db_json_t *j, const db_device_info_t *dev)
{
    const db_display_t *d = &dev->displays[0];
    db_json_int(j, "width", d->width);
    db_json_int(j, "height", d->height);
    db_json_arr_open(j, "keys");
    for (uint8_t k = 0; k < dev->key_count; k++) {
        for (uint8_t t = 0; t < d->target_count; t++) {
            if (d->targets[t].kind == DB_TARGET_KEY && d->targets[t].index == k) {
                write_rect(j, d->targets[t].rect);
            }
        }
    }
    db_json_arr_close(j);
    db_json_arr_open(j, "touch");
    for (uint8_t t = 0; t < dev->touch_count; t++) {
        write_rect(j, dev->touch[t].rect);
    }
    db_json_arr_close(j);
    db_json_int(j, "encoders", dev->encoder_count);
    int last;
    bool on_encoders;
    bool any = db_ui_menu_chord(dev->encoder_count, db_device_image_keys(dev), &on_encoders, &last);
    db_json_obj_open(j, "menu_chord");
    db_json_str(j, "on", !any ? "off" : on_encoders ? "encoders" : "keys");
    if (any) {
        db_json_int(j, "first", 0);
        db_json_int(j, "last", last);
        db_json_int(j, "hold_ms", db_config()->menu_hold_ms);
    }
    db_json_obj_close(j);
}

static size_t screen_json_with(int deck, char *buf, size_t cap, db_shot_t *shots, db_device_info_t *info)
{
    db_source_t writer;
    size_t n = db_deck_shots(deck, &writer, shots, DB_SCREEN_MAX);
    bool present = db_deck_info(deck, info);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "writer", db_deck_source_name(writer));
    if (present) {
        write_geometry(&j, info);
    }
    db_json_arr_open(&j, "images");
    for (size_t i = 0; present && i < n; i++) {
        char url[32];
        snprintf(url, sizeof(url), "/api/screen/%lu.jpg", (unsigned long)shots[i].id);
        db_json_obj_open(&j, NULL);
        write_fields(&j, shots[i].rect);
        db_json_str(&j, "url", url);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t screen_json(char *buf, size_t cap)
{
    static db_shot_t shots[DB_SCREEN_MAX];
    return screen_json_with(web_deck, buf, cap, shots, &dev);
}

/* For the event task, which runs outside the handler lock: buffers of its own. */
size_t api_screen_json(int deck, char *buf, size_t cap)
{
    static db_shot_t shots[DB_SCREEN_MAX];
    static db_device_info_t info;
    return screen_json_with(deck, buf, cap, shots, &info);
}

/* The menu and the setup pages draw the web password: their picture is for the password, not an API key. The
 * other deck routes only check web_deck_refused, so a key can still close the menu and press keys. */
static bool screen_hidden(httpd_req_t *req)
{
    if (!auth_by_token(req) || !db_ui_password_shown()) {
        return false;
    }
    send_error(req, "403 Forbidden", "the deck shows the web password: for the password only");
    return true;
}

esp_err_t get_screen(httpd_req_t *req)
{
    if (screen_hidden(req)) {
        return ESP_OK;
    }
    return send_built(req, SCREEN_JSON_MAX, screen_json);
}

/* An image never changes under its id, so browsers may keep it. It comes upright, as a JPEG. */
esp_err_t get_screen_image(httpd_req_t *req)
{
    if (screen_hidden(req)) {
        return ESP_OK;
    }
    unsigned long id = strtoul(req->uri + strlen("/api/screen/"), NULL, 10);
    uint8_t *data = NULL;
    size_t len = 0;
    esp_err_t found = ESP_ERR_NOT_FOUND; /* ids are unique over all decks: the URL needs no deck */
    for (int d = 0; d < DB_MAX_DECKS && found == ESP_ERR_NOT_FOUND; d++) {
        found = db_deck_shot_upright(d, (uint32_t)id, &data, &len);
    }
    if (found == ESP_ERR_NO_MEM) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    if (found == ESP_ERR_NOT_FOUND) {
        return send_error(req, "404 Not Found", "image no longer shown");
    }
    if (found != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(found));
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "private, max-age=3600, immutable");
    esp_err_t err = httpd_resp_send(req, (const char *)data, (ssize_t)len);
    heap_caps_free(data);
    return err;
}

/* The picture on the deck as one JPEG, for a camera in Home Assistant. */
esp_err_t get_screen_jpg(httpd_req_t *req)
{
    if (screen_hidden(req)) {
        return ESP_OK;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = db_ui_snapshot(web_deck, &jpeg, &len);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "no deck attached");
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

esp_err_t post_ota_url(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    bool ok = cJSON_IsString(url) &&
              (strncmp(url->valuestring, "http://", 7) == 0 || strncmp(url->valuestring, "https://", 8) == 0);
    esp_err_t err = ok ? db_ota_start_url(url->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "url: http(s)://… to a signed .dbfw file expected");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "an update is already running");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    /* progress shows on the deck and in the state of /api/ws; the bridge restarts when done */
    return send_json(req, "{\"ok\":true}");
}

#if CONFIG_IDF_TARGET_LINUX
esp_err_t get_coredump(httpd_req_t *req)
{
    return send_error(req, "404 Not Found", "no coredump");
}

esp_err_t delete_coredump(httpd_req_t *req)
{
    return send_json(req, "{\"ok\":true}");
}
#else
static size_t coredump_json(char *buf, size_t cap)
{
    esp_core_dump_summary_t *sum = heap_caps_malloc(sizeof(*sum), MALLOC_CAP_SPIRAM); /* 1.2 KB, stack dump */
    size_t addr = 0;
    size_t size = 0;
    esp_core_dump_image_get(&addr, &size);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_int(&j, "size", (int64_t)size);
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
        char hex[12];
        db_json_str(&j, "task", sum->exc_task);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->exc_pc);
        db_json_str(&j, "pc", hex);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->ex_info.ra);
        db_json_str(&j, "ra", hex);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->ex_info.sp);
        db_json_str(&j, "sp", hex);
        db_json_int(&j, "mcause", sum->ex_info.mcause);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->ex_info.mtval);
        db_json_str(&j, "mtval", hex);
        db_json_str(&j, "elf_sha256", (const char *)sum->app_elf_sha256);
    } else {
        db_json_bool(&j, "summary", false);
    }
    heap_caps_free(sum);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

/* GET /api/coredump: what crashed. ?raw=1: the ELF dump itself, for `idf.py coredump-info -c` with the
 * build's ELF. */
esp_err_t get_coredump(httpd_req_t *req)
{
    size_t addr = 0;
    size_t size = 0;
    if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        return send_error(req, "404 Not Found", "no coredump");
    }
    if (!strstr(req->uri, "raw=1")) {
        return send_built(req, 512, coredump_json);
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    char *chunk = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!part || !chunk) {
        heap_caps_free(chunk);
        return send_error(req, "500 Internal Server Error", "memory");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = ESP_OK;
    for (size_t off = 0; off < size && err == ESP_OK; off += 4096) {
        size_t n = size - off < 4096 ? size - off : 4096;
        err = esp_partition_read(part, addr - part->address + off, chunk, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, chunk, (ssize_t)n);
        }
    }
    heap_caps_free(chunk);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

esp_err_t delete_coredump(httpd_req_t *req)
{
    esp_err_t err = esp_core_dump_image_erase();
    return err == ESP_OK ? send_json(req, "{\"ok\":true}")
                         : send_error(req, "500 Internal Server Error", esp_err_to_name(err));
}
#endif

#ifdef DB_DEBUG_CRASH
/* Only in test builds (idf.py -DDB_DEBUG_CRASH=1): proves a crash leaves a dump that GET /api/coredump reads.
 */
esp_err_t post_debug_crash(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    *(volatile int *)0 = 0;
    return ESP_OK;
}
#endif
