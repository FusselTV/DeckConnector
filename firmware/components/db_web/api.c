#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_json.h"
#include "db_log.h"
#include "db_ota.h"
#include "db_screen.h"
#include "db_ui.h"
#include "db_web.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "web.h"

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192
#define STATUS_JSON_MAX 4096

static const char *TAG = "api";

size_t (*api_status_json)(char *buf, size_t cap);

static int fail(char *reply, size_t cap, int status, const char *msg)
{
    snprintf(reply, cap, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return status;
}

/* Brightness, controller, host, servers and their pins, satellite, dock pairing, https_only, cors_origins and
 * setup_done apply at once; everything else needs a restart to take effect cleanly. */
int api_config(const char *body, char *reply, size_t cap)
{
    static db_config_t old; /* two of them would crowd the caller's stack */
    static db_config_t cfg;
    db_config_copy(&old);
    cfg = old;
    char err[96];
    if (db_config_from_json(&cfg, body, err, sizeof(err)) != ESP_OK) {
        return fail(reply, cap, 400, err);
    }
    if (db_config_save(&cfg) != ESP_OK) {
        return fail(reply, cap, 500, "saving failed");
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
    live.https_only = old.https_only;
    memcpy(live.cors_origins, old.cors_origins, sizeof(old.cors_origins));
    bool restart = memcmp(&live, &old, sizeof(old)) != 0;
    if (cfg.brightness != old.brightness) {
        db_deck_set_brightness(cfg.brightness);
    }
    snprintf(reply, cap, "{\"ok\":true,\"restart\":%s}", restart ? "true" : "false");
    if (restart) {
        ESP_LOGW(TAG, "config changed, restarting");
        restart_soon();
    }
    return 200;
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

int api_input(const char *body, char *reply, size_t cap)
{
    static db_device_info_t dev; /* too big for the caller's stack; callers hold web_lock() */
    if (!db_deck_info(&dev)) {
        return fail(reply, cap, 409, "no Stream Deck attached");
    }
    cJSON *root = cJSON_Parse(body);
    db_input_t ev;
    db_input_t then = {.kind = 0};
    const char *err = root ? read_input(root, &dev, &ev, &then) : "invalid JSON";
    bool real = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "real"));
    cJSON_Delete(root);
    if (err) {
        return fail(reply, cap, 400, err);
    }
    esp_err_t sent = db_deck_simulate(&ev, real);
    if (sent == ESP_OK && then.kind) {
        sent = db_deck_simulate(&then, real);
    }
    if (sent == ESP_ERR_INVALID_STATE) {
        return fail(reply, cap, 409, "no Stream Deck attached");
    }
    if (sent != ESP_OK) {
        return fail(reply, cap, 503, "input queue full");
    }
    snprintf(reply, cap, "{\"ok\":true}");
    return 200;
}

esp_err_t get_status(httpd_req_t *req)
{
    return send_built(req, STATUS_JSON_MAX, api_status_json);
}

esp_err_t get_config(httpd_req_t *req)
{
    char buf[JSON_MAX];
    db_config_to_json(db_config(), false, buf, sizeof(buf));
    return send_json(req, buf);
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

esp_err_t put_config(httpd_req_t *req)
{
    return body_api(req, api_config);
}

/* `?deck=<serial>` picks a deck; without it the first. A deck that is not attached gets a 404. */
static bool deck_refused(httpd_req_t *req)
{
    char query[64];
    char want[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "deck", want, sizeof(want)) != ESP_OK) {
        return false;
    }
    static db_device_info_t dev; /* too big for the stack; callers hold web_lock() */
    if (db_deck_info(&dev) && strcmp(dev.serial, want) == 0) {
        return false;
    }
    send_error(req, "404 Not Found", "no such deck");
    return true;
}

esp_err_t post_input(httpd_req_t *req)
{
    if (deck_refused(req)) {
        return ESP_OK;
    }
    return body_api(req, api_input);
}

esp_err_t post_menu(httpd_req_t *req)
{
    if (deck_refused(req)) {
        return ESP_OK;
    }
    if (db_ui_open_menu() != ESP_OK) {
        return send_error(req, "409 Conflict", "no deck connected");
    }
    return send_json(req, "{\"ok\":true}");
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
    if (cJSON_IsBool(accept)) {
        db_dock_answer(cJSON_IsTrue(accept));
    } else if (cJSON_IsNumber(pairing) && pairing->valueint >= 0 && pairing->valueint <= 3600) {
        db_dock_pairing(pairing->valueint);
    } else if (cJSON_IsTrue(forget) || cJSON_IsString(forget)) {
        db_dock_forget(cJSON_IsString(forget) ? forget->valuestring : NULL);
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    return ok ? send_json(req, "{\"ok\":true}")
              : send_error(req, "400 Bad Request", "accept, pairing (0 to 3600 s) or forget expected");
}

esp_err_t post_deck_reconnect(httpd_req_t *req)
{
    if (deck_refused(req)) {
        return ESP_OK;
    }
    db_deck_reconnect();
    return send_json(req, "{\"ok\":true}");
}

/* `?slot=N` writes into that app slot, `?boot=0` keeps the running firmware. */
esp_err_t post_ota(httpd_req_t *req)
{
    char q[32] = "", v[8];
    int slot = -1;
    bool boot = true;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "slot", v, sizeof(v)) == ESP_OK) {
            slot = atoi(v);
        }
        if (httpd_query_key_value(q, "boot", v, sizeof(v)) == ESP_OK) {
            boot = strcmp(v, "0") != 0;
        }
    }
    if (db_ota_begin(req->content_len, slot, boot) != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running or the file is empty");
    }
    /* Flash writes stall the network between chunks, so the whole file comes in first when PSRAM allows:
     * 1.5 MB took 28 s streamed. */
    size_t cap = req->content_len;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        cap = CHUNK;
        buf = heap_caps_malloc(cap, MALLOC_CAP_INTERNAL);
    }
    size_t left = req->content_len, have = 0;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
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
    if (err != ESP_OK) {
        db_ota_abort(esp_err_to_name(err));
        return send_error(req, "400 Bad Request", esp_err_to_name(err));
    }
    return send_json(req, boot ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true}");
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
        db_json_int(&j, "slot", i);
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
        return send_error(req, "400 Bad Request",
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
    esp_err_t err = db_ota_boot_slot(slot);
    if (err != ESP_OK) {
        return send_error(req, err == ESP_ERR_NOT_FOUND ? "404 Not Found" : "409 Conflict",
                          err == ESP_ERR_INVALID_VERSION ? "slot not bootable"
                          : err == ESP_ERR_INVALID_STATE ? "the running firmware has not confirmed itself yet"
                                                         : esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"restart\":true}");
}

esp_err_t post_reboot(httpd_req_t *req)
{
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
    size_t n = db_log_read(buf, 16 * 1024);
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
    bool on_encoders = db_ui_menu_chord(dev->encoder_count, dev->key_count, &last);
    db_json_obj_open(j, "menu_chord");
    db_json_str(j, "on", on_encoders ? "encoders" : "keys");
    db_json_int(j, "first", 0);
    db_json_int(j, "last", last);
    db_json_obj_close(j);
}

static size_t screen_json(char *buf, size_t cap)
{
    static db_device_info_t dev; /* too big for the stack; timed() runs one handler at a time */
    static db_shot_t shots[DB_SCREEN_MAX];
    db_source_t writer;
    size_t n = db_deck_shots(&writer, shots, DB_SCREEN_MAX);
    bool present = db_deck_info(&dev);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "writer", db_deck_source_name(writer));
    if (present) {
        write_geometry(&j, &dev);
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

esp_err_t get_screen(httpd_req_t *req)
{
    if (deck_refused(req)) {
        return ESP_OK;
    }
    return send_built(req, SCREEN_JSON_MAX, screen_json);
}

/* An image never changes under its id, so browsers may keep it. */
esp_err_t get_screen_image(httpd_req_t *req)
{
    if (deck_refused(req)) {
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
esp_err_t get_screen_jpg(httpd_req_t *req)
{
    if (deck_refused(req)) {
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

esp_err_t post_ota_url(httpd_req_t *req)
{
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
    return send_json(req, "{\"ok\":true}");
}
