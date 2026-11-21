#include "db_config.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "db_client.h"
#include "db_device.h"
#include "db_json.h"
#include "db_matter.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

/* 2: setup_done, 3: four servers and host, 4: volume, 5: screen_dim, 6: speaker, 7: star, 8: update_auto */
#define CONFIG_VERSION 8
#define NVS_NS         "deckbridge"
#define NVS_KEY        "config"

static const char *TAG = "config";
static db_config_t current;
static SemaphoreHandle_t lock;
static SemaphoreHandle_t edit;

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

bool db_config_screen_off_ok(long s)
{
    return s == 0 || (s >= 10 && s <= 86400);
}

bool db_config_screen_dim_ok(long pct)
{
    return pct >= 0 && pct <= 90 && pct % 10 == 0;
}

void db_config_board_id(char out[13])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
                       .lang = "de",
                       .max_decks = 2,
                       .usb_speed = "auto",
                       .update_auto = true,
                       .lan = {.dhcp = true},
                       .wlan = {.dhcp = true}};
    snprintf(c->name, sizeof(c->name), "deckconnector-%02x%02x%02x", mac[3], mac[4], mac[5]);
    db_config_random_password(c->admin_pass, 9);
}

/* web/claimed 0: any browser may set the device up without the password until one is set or setup is done
 * (db_web auth.c) */
static esp_err_t set_claimed(uint8_t claimed)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("web", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "claimed", claimed);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    return err;
}

/* lwip reads a leading zero as octal: "010" would apply .8, "08" no address at all */
static bool valid_ip(const char *s)
{
    uint32_t ip;
    return s[0] == '\0' || db_ipv4(s, strlen(s), &ip);
}

/* Older firmware took fields valid_ip refuses, and every later save of this interface would fail. Each one
 * becomes the address lwip applied ("010": .8), or empty where lwip applied none. */
static void fix_old_ips(db_ip_config_t *ip)
{
    char *fields[] = {ip->ip, ip->mask, ip->gateway, ip->dns};
    for (size_t i = 0; i < 4; i++) {
        struct in_addr a = {0};
        if (memchr(fields[i], 0, sizeof(ip->ip)) && valid_ip(fields[i])) {
            continue;
        }
        if (memchr(fields[i], 0, sizeof(ip->ip)) && inet_aton(fields[i], &a)) {
            const uint8_t *b = (const uint8_t *)&a.s_addr;
            snprintf(fields[i], sizeof(ip->ip), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        } else {
            fields[i][0] = '\0';
        }
    }
    ip->dhcp = ip->dhcp || !ip->ip[0] || !ip->mask[0];
}

esp_err_t db_config_load(void)
{
    lock = xSemaphoreCreateMutex();
    edit = xSemaphoreCreateMutex();
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
            if (current.version < 4 || current.volume > 100) {
                current.volume = 0; /* older blobs end in padding where volume now sits */
            }
            if (current.version < 5 || !db_config_screen_dim_ok(current.screen_dim)) {
                current.screen_dim = 0;
            }
            if (current.version < 6) {
                current.speaker = true; /* saved before the question existed: the sounds it had stay */
            }
            if (current.version < 7 || !memchr(current.star_fw, 0, sizeof(current.star_fw))) {
                current.star_off = false;
                current.star_fw[0] = '\0';
                current.star_until = 0;
            }
            if (current.version < 8) {
                current.update_auto = true;
            }
            current.version = CONFIG_VERSION;
            if (current.controller >= DB_CTRL_COUNT) { /* written by newer firmware before a rollback */
                current.controller = DB_CTRL_SERVER;
            }
            /* An older blob ends in the struct's padding, which lands in the first appended fields:
             * menu_chord came back empty once, and then every settings change without it was refused. */
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
            if (memcmp(current.lang, "en", sizeof(current.lang)) != 0) { /* padding of an older blob */
                strcpy(current.lang, fresh.lang);
            }
            if (current.max_decks < 1 || current.max_decks > DB_MAX_DECKS) {
                current.max_decks = fresh.max_decks;
            }
            if (!db_config_screen_off_ok((long)current.screen_off_s)) {
                current.screen_off_s = fresh.screen_off_s;
            }
            uint8_t mac[6];
            if (!db_config_bssid(current.wifi_bssid, mac)) {
                current.wifi_bssid[0] = '\0';
            }
            if (!memchr(current.ntp_server, 0, sizeof(current.ntp_server)) ||
                !db_config_ntp_ok(current.ntp_server)) {
                current.ntp_server[0] = '\0';
            }
            fix_old_ips(&current.lan);
            fix_old_ips(&current.wlan);
            uint32_t proxy;
            if (!memchr(current.trusted_proxy, 0, sizeof(current.trusted_proxy)) ||
                (current.trusted_proxy[0] &&
                 !db_ipv4(current.trusted_proxy, strlen(current.trusted_proxy), &proxy))) {
                current.trusted_proxy[0] = '\0';
            }
            uint8_t lock;
            memcpy(&lock, &current.qr_login_off,
                   1); /* padding of an older blob: neither 0 nor 1 is no bool */
            current.qr_login_off = lock == 1;
            memcpy(&lock, &current.satellite_auto_off, 1);
            current.satellite_auto_off = lock == 1;
        }
        free(blob);
    }
    nvs_close(h);
    /* Serial console only matters with the device in hand; the deck menu shows the same. */
    esp_rom_printf("web login: admin / %s\n",
                   current.admin_pass); /* serial only: not in the log /api/log serves */
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "first start, saving defaults");
        err = db_config_save(&current);
        return err == ESP_OK ? set_claimed(0) : err;
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
    if (err == ESP_OK && cfg->setup_done && !current.setup_done && set_claimed(1) != ESP_OK) {
        ESP_LOGE(TAG, "setup done, but the setup claim not closed for the next start");
    }
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

void db_config_edit_begin(void)
{
    xSemaphoreTake(edit, portMAX_DELAY);
}

void db_config_edit_end(void)
{
    xSemaphoreGive(edit);
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

static esp_err_t erase(const char *ns, const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = key ? nvs_erase_key(h, key) : nvs_erase_all(h);
        err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset: %s %s not erased: %s", ns, key ? key : "", esp_err_to_name(err));
    }
    return err;
}

esp_err_t db_config_reset(void)
{
    /* "ota" stays (each slot's security level); the device CA goes, so a board given away cannot vouch for
     * its old name to the devices that trusted it */
    const char *const wipe[][2] = {
        {"auth", NULL},  {"dock", NULL}, {"satellite", NULL}, {"web", "ccert"},     {"web", "ckey"},
        {"web", "step"}, {"web", "ca"},  {"web", "cakey"},    {"web", "caname"},    {"web", "cert"},
        {"web", "key"},  {"web", "san"}, {"web", "until"},    {DB_MATTER_NVS, NULL}};
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < sizeof(wipe) / sizeof(wipe[0]); i++) {
        esp_err_t e = erase(wipe[i][0], wipe[i][1]);
        err = err == ESP_OK ? e : err;
    }
    db_config_t c;
    defaults(&c);
    esp_err_t saved = db_config_save(&c);
    err = err == ESP_OK ? saved : err;
    return err == ESP_OK ? set_claimed(0) : err;
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
    db_json_int(&j, "volume", cfg->volume);
    db_json_bool(&j, "speaker", cfg->speaker);
    ip_to_json(&j, "lan", &cfg->lan);
    ip_to_json(&j, "wlan", &cfg->wlan);
    db_json_str(&j, "wifi_ssid", cfg->wifi_ssid);
    db_json_bool(&j, "wifi_pass_set", cfg->wifi_pass[0] != '\0');
    db_json_str(&j, "wifi2_ssid", cfg->wifi2_ssid);
    db_json_bool(&j, "wifi2_pass_set", cfg->wifi2_pass[0] != '\0');
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
    db_json_str(&j, "lang", cfg->lang);
    db_json_str(&j, "usb_speed", cfg->usb_speed);
    db_json_str(&j, "wifi_bssid", cfg->wifi_bssid);
    db_json_str(&j, "ntp_server", cfg->ntp_server);
    db_json_str(&j, "trusted_proxy", cfg->trusted_proxy);
    db_json_bool(&j, "qr_login", !cfg->qr_login_off);
    db_json_bool(&j, "satellite_auto", !cfg->satellite_auto_off);
    db_json_int(&j, "menu_hold_ms", cfg->menu_hold_ms);
    db_json_int(&j, "max_decks", cfg->max_decks);
    db_json_int(&j, "screen_off_s", (int)cfg->screen_off_s);
    db_json_int(&j, "screen_dim", cfg->screen_dim);
    db_json_bool(&j, "star_off", cfg->star_off);
    db_json_str(&j, "star_fw", cfg->star_fw);
    db_json_int(&j, "star_until", (int)cfg->star_until);
    db_json_bool(&j, "update_auto", cfg->update_auto);
    db_json_bool(&j, "token_set", cfg->token[0] != '\0');
    if (with_secrets) { /* exactly the keys PUT /api/config takes back */
        db_json_str(&j, "admin_pass", cfg->admin_pass);
        db_json_str(&j, "wifi_pass", cfg->wifi_pass);
        db_json_str(&j, "wifi2_pass", cfg->wifi2_pass);
        db_json_str(&j, "token", cfg->token);
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
        snprintf(err, err_cap,
                 strcmp(key, "admin_pass") ? "%s: text expected, at most %u characters"
                                           : "%s: 6 to %u characters",
                 key, (unsigned)(cap - 1));
        return false;
    }
    strcpy(dst, v->valuestring);
    return true;
}

bool db_config_ntp_ok(const char *s)
{
    return s[strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:")] == '\0';
}

bool db_config_bssid(const char *text, uint8_t out[6])
{
    unsigned b[6];
    char end;
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &end) != 6 ||
        strlen(text) != 17) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[i];
    }
    return true;
}

bool db_config_wifi_pass_ok(const char *p)
{
    size_t n = strlen(p);
    return n == 0 || (n >= 8 && n <= 63) || (n == 64 && p[strspn(p, "0123456789abcdefABCDEF")] == '\0');
}

/* A name that works as host name and mDNS name: letters, digits and inner dashes. */
static bool name_ok(const char *s)
{
    size_t n = strlen(s);
    return n && s[0] != '-' && s[n - 1] != '-' &&
           s[strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")] == '\0';
}

static uint32_t ip_bits(const char *s)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d);
    return a << 24 | b << 16 | c << 8 | d;
}

/* A fixed address that leaves the bridge reachable: a /1 to /30 mask, the address no network or broadcast
 * address and not in 0/8, 127/8 or 224/3, the gateway in the same network and not the address itself. */
static const char *fixed_ip_problem(const db_ip_config_t *ip)
{
    uint32_t addr = ip_bits(ip->ip), mask = ip_bits(ip->mask), gw = ip_bits(ip->gateway);
    if (!mask || mask > 0xfffffffc || (~mask & (~mask + 1))) {
        return "mask: 128.0.0.0 to 255.255.255.252 expected";
    }
    if (!(addr & ~mask) || (addr & ~mask) == ~mask) {
        return "ip: the network or broadcast address";
    }
    if (addr >> 24 == 0 || addr >> 24 == 127 || addr >> 24 >= 224) {
        return "ip: 0.x, 127.x and 224.x and up cannot be a device address";
    }
    if (ip->gateway[0] && ((gw & mask) != (addr & mask) || gw == addr)) {
        return "gateway: another address in the same network expected";
    }
    return NULL;
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
    const char *problem = ip->dhcp ? NULL : fixed_ip_problem(ip);
    if (problem) {
        snprintf(err, err_cap, "%s %s", key, problem);
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
             strncmp(v->valuestring, "wss://", 6) != 0) ||
            strpbrk(v->valuestring, " \"\\")) {
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

static bool whole(const cJSON *v)
{
    return cJSON_IsNumber(v) && v->valuedouble == v->valueint;
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
    const char *flags[] = {"setup_done", "dock_paired_only", "https_only",
                           "speaker",    "star_off",         "update_auto"};
    bool *fields[] = {&cfg->setup_done, &cfg->dock_paired_only, &cfg->https_only,
                      &cfg->speaker,    &cfg->star_off,         &cfg->update_auto};
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, flags[i]);
        if (v && !cJSON_IsBool(v)) {
            snprintf(err, err_cap, "%s: true or false expected", flags[i]);
            return false;
        }
        if (v) {
            *fields[i] = cJSON_IsTrue(v);
        }
    }
    const cJSON *qr = cJSON_GetObjectItemCaseSensitive(root, "qr_login");
    if (qr && !cJSON_IsBool(qr)) {
        snprintf(err, err_cap, "qr_login: true or false expected");
        return false;
    }
    if (qr) {
        cfg->qr_login_off = !cJSON_IsTrue(qr);
    }
    const cJSON *sat_auto = cJSON_GetObjectItemCaseSensitive(root, "satellite_auto");
    if (sat_auto && !cJSON_IsBool(sat_auto)) {
        snprintf(err, err_cap, "satellite_auto: true or false expected");
        return false;
    }
    if (sat_auto) {
        cfg->satellite_auto_off = !cJSON_IsTrue(sat_auto);
    }
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (b) {
        if (!whole(b) || b->valueint < 0 || b->valueint > 100) {
            snprintf(err, err_cap, "brightness: 0 to 100 expected");
            return false;
        }
        cfg->brightness = (uint8_t)b->valueint;
    }
    const cJSON *vol = cJSON_GetObjectItemCaseSensitive(root, "volume");
    if (vol) {
        if (!whole(vol) || vol->valueint < 0 || vol->valueint > 100) {
            snprintf(err, err_cap, "volume: 0 to 100 expected");
            return false;
        }
        cfg->volume = (uint8_t)vol->valueint;
    }
    const cJSON *hold = cJSON_GetObjectItemCaseSensitive(root, "menu_hold_ms");
    if (hold) {
        if (!whole(hold) || hold->valueint < 500 || hold->valueint > 10000) {
            snprintf(err, err_cap, "menu_hold_ms: 500 to 10000 expected");
            return false;
        }
        cfg->menu_hold_ms = (uint16_t)hold->valueint;
    }
    const cJSON *max = cJSON_GetObjectItemCaseSensitive(root, "max_decks");
    if (max) {
        if (!whole(max) || max->valueint < 1 || max->valueint > DB_MAX_DECKS) {
            snprintf(err, err_cap, "max_decks: 1 to %d expected", DB_MAX_DECKS);
            return false;
        }
        cfg->max_decks = (uint8_t)max->valueint;
    }
    const cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "screen_off_s");
    if (off) {
        if (!whole(off) || !db_config_screen_off_ok(off->valueint)) {
            snprintf(err, err_cap, "screen_off_s: 0 or 10 to 86400 expected");
            return false;
        }
        cfg->screen_off_s = (uint32_t)off->valueint;
    }
    const cJSON *dim = cJSON_GetObjectItemCaseSensitive(root, "screen_dim");
    if (dim) {
        if (!whole(dim) || !db_config_screen_dim_ok(dim->valueint)) {
            snprintf(err, err_cap, "screen_dim: 0 or 10 to 90 in steps of 10 expected");
            return false;
        }
        cfg->screen_dim = (uint8_t)dim->valueint;
    }
    const cJSON *until = cJSON_GetObjectItemCaseSensitive(root, "star_until");
    if (until) {
        if (!whole(until) || until->valueint < 0) {
            snprintf(err, err_cap, "star_until: Unix time expected");
            return false;
        }
        cfg->star_until = (uint32_t)until->valueint;
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
    char satellite_was[sizeof(cfg->satellite)];
    strcpy(satellite_was, cfg->satellite);
    bool ok = take_str(root, "name", cfg->name, sizeof(cfg->name), err, err_cap) &&
              take_str(root, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid), err, err_cap) &&
              take_str(root, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass), err, err_cap) &&
              take_str(root, "wifi2_ssid", cfg->wifi2_ssid, sizeof(cfg->wifi2_ssid), err, err_cap) &&
              take_str(root, "wifi2_pass", cfg->wifi2_pass, sizeof(cfg->wifi2_pass), err, err_cap) &&
              take_str(root, "token", cfg->token, sizeof(cfg->token), err, err_cap) &&
              take_str(root, "host", cfg->host, sizeof(cfg->host), err, err_cap) &&
              take_str(root, "satellite", cfg->satellite, sizeof(cfg->satellite), err, err_cap) &&
              take_str(root, "cors_origins", cfg->cors_origins, sizeof(cfg->cors_origins), err, err_cap) &&
              take_str(root, "menu_chord", cfg->menu_chord, sizeof(cfg->menu_chord), err, err_cap) &&
              take_str(root, "lang", cfg->lang, sizeof(cfg->lang), err, err_cap) &&
              take_str(root, "usb_speed", cfg->usb_speed, sizeof(cfg->usb_speed), err, err_cap) &&
              take_str(root, "wifi_bssid", cfg->wifi_bssid, sizeof(cfg->wifi_bssid), err, err_cap) &&
              take_str(root, "ntp_server", cfg->ntp_server, sizeof(cfg->ntp_server), err, err_cap) &&
              take_str(root, "trusted_proxy", cfg->trusted_proxy, sizeof(cfg->trusted_proxy), err, err_cap) &&
              take_str(root, "star_fw", cfg->star_fw, sizeof(cfg->star_fw), err, err_cap) &&
              take_str(root, "admin_pass", cfg->admin_pass, sizeof(cfg->admin_pass), err, err_cap) &&
              take_ip(root, "lan", &cfg->lan, err, err_cap) &&
              take_ip(root, "wlan", &cfg->wlan, err, err_cap) && take_servers(root, cfg, err, err_cap) &&
              take_pins(root, cfg, err, err_cap) && take_scalars(root, cfg, err, err_cap);
    if (!cJSON_GetObjectItemCaseSensitive(root, "satellite_auto") &&
        strcmp(satellite_was, cfg->satellite) != 0) {
        cfg->satellite_auto_off = !cfg->satellite[0];
    }
    if (ok && cJSON_GetObjectItemCaseSensitive(root, "name") && !name_ok(cfg->name)) {
        snprintf(err, err_cap, "name: letters, digits and dashes, no dash at the start or end");
        ok = false;
    }
    if (ok && cJSON_GetObjectItemCaseSensitive(root, "wifi_pass") &&
        !db_config_wifi_pass_ok(cfg->wifi_pass)) {
        snprintf(err, err_cap, "wifi_pass: 8 to 63 characters, 64 hex digits, or empty for an open network");
        ok = false;
    }
    if (ok && cJSON_GetObjectItemCaseSensitive(root, "wifi2_pass") &&
        !db_config_wifi_pass_ok(cfg->wifi2_pass)) {
        snprintf(err, err_cap, "wifi2_pass: 8 to 63 characters, 64 hex digits, or empty for an open network");
        ok = false;
    }
    bool url = strncmp(cfg->satellite, "ws://", 5) == 0 || strncmp(cfg->satellite, "wss://", 6) == 0;
    if (ok && strpbrk(cfg->satellite, url ? " \"" : " \"/")) {
        snprintf(err, err_cap, "satellite: host, host:port, or a ws:// or wss:// URL expected");
        ok = false;
    }
    uint8_t mac[6];
    if (ok && cfg->wifi_bssid[0] && !db_config_bssid(cfg->wifi_bssid, mac)) {
        snprintf(err, err_cap, "wifi_bssid: aa:bb:cc:dd:ee:ff or empty");
        ok = false;
    }
    uint32_t proxy;
    if (ok && cfg->trusted_proxy[0] && !db_ipv4(cfg->trusted_proxy, strlen(cfg->trusted_proxy), &proxy)) {
        snprintf(err, err_cap, "trusted_proxy: an IPv4 address like 192.168.1.15, or empty");
        ok = false;
    }
    if (ok && !db_config_ntp_ok(cfg->ntp_server)) {
        snprintf(err, err_cap, "ntp_server: a host name or address");
        ok = false;
    }
    if (ok && strcmp(cfg->usb_speed, "auto") != 0 && strcmp(cfg->usb_speed, "full") != 0) {
        snprintf(err, err_cap, "usb_speed: auto or full expected");
        ok = false;
    }
    if (ok && strcmp(cfg->lang, "de") != 0 && strcmp(cfg->lang, "en") != 0) {
        snprintf(err, err_cap, "lang: de or en expected");
        ok = false;
    }
    if (ok && !known_chord(cfg->menu_chord)) {
        snprintf(err, err_cap, "menu_chord: auto, keys, encoders or off expected");
        ok = false;
    }
    if (ok && strlen(cfg->admin_pass) < 6) {
        snprintf(err, err_cap, "admin_pass: 6 to 23 characters");
        ok = false;
    }
    cJSON_Delete(root);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
