#include "db_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "db_json.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define CONFIG_VERSION 3 /* 2: setup_done, 3: four servers and host */
#define NVS_NS         "deckbridge"
#define NVS_KEY        "config"

static const char *TAG = "config";
static db_config_t current;
static SemaphoreHandle_t lock;

void db_config_random_password(char *out, size_t len)
{
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789"; /* no look-alike characters */
    for (size_t i = 0; i + 1 < len; i++) {
        out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    out[len - 1] = '\0';
}

static void (*on_save)(void);

char *db_config_server(db_config_t *cfg, int i)
{
    return i < 2 ? cfg->servers_first[i] : cfg->servers_more[i - 2];
}

const char *db_config_server_of(const db_config_t *cfg, int i)
{
    return db_config_server((db_config_t *)cfg, i);
}

static bool known_chord(const char *chord)
{
    const char *chords[] = {"auto", "keys", "encoders", "off"};
    for (int i = 0; i < 4; i++) {
        if (strcmp(chord, chords[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void defaults(db_config_t *c)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    *c = (db_config_t){.version = CONFIG_VERSION,
                       .controller = DB_CTRL_COMPANION, /* Companion over Satellite or as a dock host */
                       .brightness = 80,
                       .menu_chord = "auto",
                       .menu_hold_ms = 2000,
                       .usb_speed = "auto",
                       .lan = {.dhcp = true},
                       .wlan = {.dhcp = true}};
    snprintf(c->name, sizeof(c->name), "deckconnector-%02x%02x%02x", mac[3], mac[4], mac[5]);
    db_config_random_password(c->admin_pass, 9);
}

esp_err_t db_config_load(void)
{
    lock = xSemaphoreCreateMutex();
    defaults(&current);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = 0;
    err = nvs_get_blob(h, NVS_KEY, NULL, &len);
    if (err == ESP_OK && len > 0) {
        /* Older blobs are shorter and the new fields keep their defaults. A longer blob comes from newer
         * firmware before a rollback; its first part is still this layout. */
        uint8_t *blob = malloc(len);
        err = blob ? nvs_get_blob(h, NVS_KEY, blob, &len) : ESP_ERR_NO_MEM;
        if (err == ESP_OK) {
            memcpy(&current, blob, len < sizeof(current) ? len : sizeof(current));
            if (current.version < 2) {
                current.setup_done = true; /* saved before the welcome steps existed: already set up */
            }
            current.version = CONFIG_VERSION;
            if (current.controller >= DB_CTRL_COUNT) { /* written by newer firmware before a rollback */
                current.controller = DB_CTRL_SERVER;
            }
            /* An older blob ends in the struct's padding, which lands in the first appended fields: menu_chord
             * came back empty once, and then every settings change without it was refused. */
            db_config_t fresh;
            defaults(&fresh);
            if (!known_chord(current.menu_chord)) {
                strcpy(current.menu_chord, fresh.menu_chord);
            }
            if (current.menu_hold_ms < 500 || current.menu_hold_ms > 10000) {
                current.menu_hold_ms = fresh.menu_hold_ms;
            }
            if (strcmp(current.usb_speed, "full") != 0) {
                strcpy(current.usb_speed, fresh.usb_speed);
            }
        }
        free(blob);
    }
    nvs_close(h);
    /* Serial console only matters with the device in hand; the deck menu shows the same. */
    esp_rom_printf("web login: admin / %s\n",
                   current.admin_pass); /* serial only: not in the log /api/log serves */
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "first start, saving defaults");
        return db_config_save(&current);
    }
    return err;
}

const db_config_t *db_config(void)
{
    return &current;
}

void db_config_copy(db_config_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = current;
    xSemaphoreGive(lock);
}

esp_err_t db_config_save(const db_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, cfg, sizeof(*cfg));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK && cfg != &current) {
        xSemaphoreTake(lock, portMAX_DELAY);
        current = *cfg;
        xSemaphoreGive(lock);
    }
    if (err == ESP_OK && on_save) {
        on_save();
    }
    return err;
}

void db_config_on_save(void (*cb)(void))
{
    on_save = cb;
}

static const char *const controller_names[DB_CTRL_COUNT] = {"server", "companion", "app"};

const char *db_controller_name(db_controller_t c)
{
    return c < DB_CTRL_COUNT ? controller_names[c] : "server";
}

esp_err_t db_config_reset(void)
{
    db_config_t c;
    defaults(&c);
    return db_config_save(&c);
}

static void ip_to_json(db_json_t *j, const char *key, const db_ip_config_t *ip)
{
    db_json_obj_open(j, key);
    db_json_bool(j, "dhcp", ip->dhcp);
    db_json_str(j, "ip", ip->ip);
    db_json_str(j, "mask", ip->mask);
    db_json_str(j, "gateway", ip->gateway);
    db_json_str(j, "dns", ip->dns);
    db_json_obj_close(j);
}

size_t db_config_to_json(const db_config_t *cfg, bool with_secrets, char *buf, size_t cap)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "name", cfg->name);
    db_json_str(&j, "controller", db_controller_name(cfg->controller));
    db_json_str(&j, "host", cfg->host);
    db_json_str(&j, "satellite", cfg->satellite);
    db_json_bool(&j, "dock_paired_only", cfg->dock_paired_only);
    db_json_bool(&j, "https_only", cfg->https_only);
    db_json_bool(&j, "setup_done", cfg->setup_done);
    db_json_int(&j, "brightness", cfg->brightness);
    ip_to_json(&j, "lan", &cfg->lan);
    ip_to_json(&j, "wlan", &cfg->wlan);
    db_json_str(&j, "wifi_ssid", cfg->wifi_ssid);
    db_json_bool(&j, "wifi_pass_set", cfg->wifi_pass[0] != '\0');
    db_json_arr_open(&j, "servers");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        db_json_str(&j, NULL, db_config_server_of(cfg, i));
    }
    db_json_arr_close(&j);
    db_json_arr_open(&j, "server_pins");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        db_json_str(&j, NULL, cfg->server_pins[i]);
    }
    db_json_arr_close(&j);
    db_json_str(&j, "satellite_pin", cfg->satellite_pin);
    db_json_str(&j, "cors_origins", cfg->cors_origins);
    db_json_str(&j, "menu_chord", cfg->menu_chord);
    db_json_str(&j, "usb_speed", cfg->usb_speed);
    db_json_int(&j, "menu_hold_ms", cfg->menu_hold_ms);
    db_json_bool(&j, "token_set", cfg->token[0] != '\0');
    if (with_secrets) {
        db_json_str(&j, "admin_pass", cfg->admin_pass);
    }
    db_json_obj_close(&j);
    return db_json_end(&j);
}

/* Copy a string member if present. Fails when it does not fit. */
static bool take_str(const cJSON *obj, const char *key, char *dst, size_t cap, char *err, size_t err_cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!v) {
        return true;
    }
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) {
        snprintf(err, err_cap, "%s: text expected, at most %u characters", key, (unsigned)(cap - 1));
        return false;
    }
    strcpy(dst, v->valuestring);
    return true;
}

static bool valid_ip(const char *s)
{
    unsigned a, b, c, d;
    char end;
    return s[0] == '\0' ||
           (sscanf(s, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &end) == 4 && (a | b | c | d) < 256);
}

static bool take_ip(const cJSON *root, const char *key, db_ip_config_t *ip, char *err, size_t err_cap)
{
    const cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!obj) {
        return true;
    }
    const cJSON *dhcp = cJSON_GetObjectItemCaseSensitive(obj, "dhcp");
    if (cJSON_IsBool(dhcp)) {
        ip->dhcp = cJSON_IsTrue(dhcp);
    }
    if (!take_str(obj, "ip", ip->ip, sizeof(ip->ip), err, err_cap) ||
        !take_str(obj, "mask", ip->mask, sizeof(ip->mask), err, err_cap) ||
        !take_str(obj, "gateway", ip->gateway, sizeof(ip->gateway), err, err_cap) ||
        !take_str(obj, "dns", ip->dns, sizeof(ip->dns), err, err_cap)) {
        return false;
    }
    if (!valid_ip(ip->ip) || !valid_ip(ip->mask) || !valid_ip(ip->gateway) || !valid_ip(ip->dns)) {
        snprintf(err, err_cap, "%s: invalid IPv4 address", key);
        return false;
    }
    if (!ip->dhcp && (ip->ip[0] == '\0' || ip->mask[0] == '\0')) {
        snprintf(err, err_cap, "%s: a fixed address needs ip and mask", key);
        return false;
    }
    return true;
}

static bool take_servers(const cJSON *root, db_config_t *cfg, char *err, size_t err_cap)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "servers");
    if (!arr) {
        return true;
    }
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) > DB_MAX_SERVERS) {
        snprintf(err, err_cap, "servers: list of at most %d URLs expected", DB_MAX_SERVERS);
        return false;
    }
    for (int k = 0; k < DB_MAX_SERVERS; k++) {
        db_config_server(cfg, k)[0] = '\0';
    }
    int i = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr)
    {
        if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizeof(cfg->servers_first[0]) ||
            (v->valuestring[0] && strncmp(v->valuestring, "ws://", 5) != 0 &&
             strncmp(v->valuestring, "wss://", 6) != 0)) {
            snprintf(err, err_cap, "servers: ws:// or wss:// URL expected");
            return false;
        }
        strcpy(db_config_server(cfg, i++), v->valuestring);
    }
    return true;
}

/* A fingerprint as tools print it ("AB:CD:…", or without colons) to 64 lower case hex digits. */
static bool take_pin(const cJSON *v, char out[65])
{
    if (!cJSON_IsString(v)) {
        return false;
    }
    size_t n = 0;
    for (const char *c = v->valuestring; *c; c++) {
        if (*c == ':' || *c == ' ') {
            continue;
        }
        if (n == 64 || !isxdigit((unsigned char)*c)) {
            return false;
        }
        out[n++] = (char)tolower((unsigned char)*c);
    }
    out[n] = '\0';
    return n == 0 || n == 64;
}

static bool take_pins(const cJSON *root, db_config_t *cfg, char *err, size_t err_cap)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "server_pins");
    const cJSON *sat = cJSON_GetObjectItemCaseSensitive(root, "satellite_pin");
    if (arr && (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) > DB_MAX_SERVERS)) {
        snprintf(err, err_cap, "server_pins: list of at most %d fingerprints expected", DB_MAX_SERVERS);
        return false;
    }
    for (int i = 0; arr && i < DB_MAX_SERVERS; i++) {
        const cJSON *v = cJSON_GetArrayItem(arr, i);
        cfg->server_pins[i][0] = '\0';
        if (v && !take_pin(v, cfg->server_pins[i])) {
            snprintf(err, err_cap, "server_pins: SHA-256 fingerprint expected");
            return false;
        }
    }
    if (sat && !take_pin(sat, cfg->satellite_pin)) {
        snprintf(err, err_cap, "satellite_pin: SHA-256 fingerprint expected");
        return false;
    }
    return true;
}

bool db_config_pin(const char *hex, uint8_t out[32])
{
    for (int i = 0; hex[0] && i < 32; i++) {
        unsigned b;
        if (sscanf(hex + 2 * i, "%2x", &b) != 1) {
            return false; /* a short or broken pin from newer firmware's blob: no pin rather than garbage */
        }
        out[i] = (uint8_t)b;
    }
    return hex[0] != '\0';
}

static bool take_scalars(const cJSON *root, db_config_t *cfg, char *err, size_t err_cap)
{
    const cJSON *ctrl = cJSON_GetObjectItemCaseSensitive(root, "controller");
    if (ctrl) {
        int found = -1;
        for (int i = 0; i < DB_CTRL_COUNT && cJSON_IsString(ctrl); i++) {
            if (strcmp(ctrl->valuestring, controller_names[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            snprintf(err, err_cap, "controller: server, app or companion expected");
            return false;
        }
        cfg->controller = (db_controller_t)found;
    }
    const char *flags[] = {"setup_done", "dock_paired_only", "https_only"};
    bool *fields[] = {&cfg->setup_done, &cfg->dock_paired_only, &cfg->https_only};
    for (int i = 0; i < 3; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, flags[i]);
        if (v && !cJSON_IsBool(v)) {
            snprintf(err, err_cap, "%s: true or false expected", flags[i]);
            return false;
        }
        if (v) {
            *fields[i] = cJSON_IsTrue(v);
        }
    }
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (b) {
        if (!cJSON_IsNumber(b) || b->valueint < 0 || b->valueint > 100) {
            snprintf(err, err_cap, "brightness: 0 to 100 expected");
            return false;
        }
        cfg->brightness = (uint8_t)b->valueint;
    }
    const cJSON *hold = cJSON_GetObjectItemCaseSensitive(root, "menu_hold_ms");
    if (hold) {
        if (!cJSON_IsNumber(hold) || hold->valueint < 500 || hold->valueint > 10000) {
            snprintf(err, err_cap, "menu_hold_ms: 500 to 10000 expected");
            return false;
        }
        cfg->menu_hold_ms = (uint16_t)hold->valueint;
    }
    return true;
}

esp_err_t db_config_from_json(db_config_t *cfg, const char *json, char *err, size_t err_cap)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        snprintf(err, err_cap, "JSON object expected");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    bool ok = take_str(root, "name", cfg->name, sizeof(cfg->name), err, err_cap) &&
              take_str(root, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid), err, err_cap) &&
              take_str(root, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass), err, err_cap) &&
              take_str(root, "token", cfg->token, sizeof(cfg->token), err, err_cap) &&
              take_str(root, "host", cfg->host, sizeof(cfg->host), err, err_cap) &&
              take_str(root, "satellite", cfg->satellite, sizeof(cfg->satellite), err, err_cap) &&
              take_str(root, "cors_origins", cfg->cors_origins, sizeof(cfg->cors_origins), err, err_cap) &&
              take_str(root, "menu_chord", cfg->menu_chord, sizeof(cfg->menu_chord), err, err_cap) &&
              take_str(root, "usb_speed", cfg->usb_speed, sizeof(cfg->usb_speed), err, err_cap) &&
              take_str(root, "admin_pass", cfg->admin_pass, sizeof(cfg->admin_pass), err, err_cap) &&
              take_ip(root, "lan", &cfg->lan, err, err_cap) &&
              take_ip(root, "wlan", &cfg->wlan, err, err_cap) && take_servers(root, cfg, err, err_cap) &&
              take_pins(root, cfg, err, err_cap) && take_scalars(root, cfg, err, err_cap);
    bool url = strncmp(cfg->satellite, "ws://", 5) == 0 || strncmp(cfg->satellite, "wss://", 6) == 0;
    if (ok && strpbrk(cfg->satellite, url ? " \"" : " \"/")) {
        snprintf(err, err_cap, "satellite: host, host:port, or a ws:// or wss:// URL expected");
        ok = false;
    }
    if (ok && strcmp(cfg->usb_speed, "auto") != 0 && strcmp(cfg->usb_speed, "full") != 0) {
        snprintf(err, err_cap, "usb_speed: auto or full expected");
        ok = false;
    }
    if (ok && !known_chord(cfg->menu_chord)) {
        snprintf(err, err_cap, "menu_chord: auto, keys, encoders or off expected");
        ok = false;
    }
    if (ok && strlen(cfg->admin_pass) < 6) {
        snprintf(err, err_cap, "admin_pass: at least 6 characters");
        ok = false;
    }
    cJSON_Delete(root);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
