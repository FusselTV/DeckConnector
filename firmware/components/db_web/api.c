#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "db_config.h"
#include "db_deck.h"
#include "esp_log.h"
#include "web.h"

static const char *TAG = "api";

static int fail(char *reply, size_t cap, int status, const char *msg)
{
    snprintf(reply, cap, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return status;
}

/* Brightness, controller and setup_done apply at once; everything else needs a restart to take effect
 * cleanly. */
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
    static db_device_info_t dev; /* too big for the caller's stack; one request at a time per caller */
    if (!db_deck_info(&dev)) {
        return fail(reply, cap, 409, "no Stream Deck attached");
    }
    cJSON *root = cJSON_Parse(body);
    db_input_t ev;
    db_input_t then = {.kind = 0};
    const char *err = root ? read_input(root, &dev, &ev, &then) : "invalid JSON";
    cJSON_Delete(root);
    if (err) {
        return fail(reply, cap, 400, err);
    }
    esp_err_t sent = db_deck_simulate(&ev);
    if (sent == ESP_OK && then.kind) {
        sent = db_deck_simulate(&then);
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
