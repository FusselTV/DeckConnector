#include "db_config.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_json.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"

#define CONFIG_VERSION 1
#define NVS_NS         "deckbridge"
#define NVS_KEY        "config"

static const char *TAG = "config";
static db_config_t current;
static SemaphoreHandle_t lock;

static void random_password(char *out, size_t len)
{
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789"; /* no look-alike characters */
    for (size_t i = 0; i + 1 < len; i++) {
        out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    out[len - 1] = '\0';
}

static void defaults(db_config_t *c)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    *c = (db_config_t){.version = CONFIG_VERSION,
                       .mode = DB_MODE_NATIVE,
                       .brightness = 80,
                       .lan = {.dhcp = true},
                       .wlan = {.dhcp = true}};
    snprintf(c->name, sizeof(c->name), "deckbridge-%02x%02x%02x", mac[3], mac[4], mac[5]);
    random_password(c->admin_pass, 9);
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
    if (err == ESP_OK && len > 0 && len <= sizeof(current)) {
        /* Older blobs are shorter; the fields they lack keep their defaults. */
        err = nvs_get_blob(h, NVS_KEY, &current, &len);
        current.version = CONFIG_VERSION;
    }
    nvs_close(h);
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
    return err;
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
    db_json_str(&j, "mode", cfg->mode == DB_MODE_DOCK ? "dock" : "native");
    db_json_int(&j, "brightness", cfg->brightness);
    ip_to_json(&j, "lan", &cfg->lan);
    ip_to_json(&j, "wlan", &cfg->wlan);
    db_json_str(&j, "wifi_ssid", cfg->wifi_ssid);
    db_json_bool(&j, "wifi_pass_set", cfg->wifi_pass[0] != '\0');
    db_json_arr_open(&j, "servers");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        db_json_str(&j, NULL, cfg->servers[i]);
    }
    db_json_arr_close(&j);
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
    ip4_addr_t a;
    return s[0] == '\0' || ip4addr_aton(s, &a);
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
    memset(cfg->servers, 0, sizeof(cfg->servers));
    int i = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr)
    {
        if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizeof(cfg->servers[0]) ||
            (v->valuestring[0] && strncmp(v->valuestring, "ws://", 5) &&
             strncmp(v->valuestring, "wss://", 6))) {
            snprintf(err, err_cap, "servers: ws:// or wss:// URL expected");
            return false;
        }
        strcpy(cfg->servers[i++], v->valuestring);
    }
    return true;
}

static bool take_scalars(const cJSON *root, db_config_t *cfg, char *err, size_t err_cap)
{
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode) {
        if (!cJSON_IsString(mode) ||
            (strcmp(mode->valuestring, "native") && strcmp(mode->valuestring, "dock"))) {
            snprintf(err, err_cap, "mode: native or dock expected");
            return false;
        }
        cfg->mode = strcmp(mode->valuestring, "dock") == 0 ? DB_MODE_DOCK : DB_MODE_NATIVE;
    }
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (b) {
        if (!cJSON_IsNumber(b) || b->valueint < 0 || b->valueint > 100) {
            snprintf(err, err_cap, "brightness: 0 to 100 expected");
            return false;
        }
        cfg->brightness = (uint8_t)b->valueint;
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
              take_str(root, "admin_pass", cfg->admin_pass, sizeof(cfg->admin_pass), err, err_cap) &&
              take_ip(root, "lan", &cfg->lan, err, err_cap) &&
              take_ip(root, "wlan", &cfg->wlan, err, err_cap) && take_servers(root, cfg, err, err_cap) &&
              take_scalars(root, cfg, err, err_cap);
    if (ok && strlen(cfg->admin_pass) < 6) {
        snprintf(err, err_cap, "admin_pass: at least 6 characters");
        ok = false;
    }
    cJSON_Delete(root);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
