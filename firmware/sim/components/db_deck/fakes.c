/* Stand-ins for everything the API layer calls on the chip: Stream Deck + decks that are always attached
 * (DB_SIM_DECKS of them, 1 by default, serials SIM0001 and up; the ones over max_decks stay dark), no dock
 * hosts, no updates. Simulated input goes to the listeners, so the web socket reports it like on the device.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_satellite.h"
#include "db_ui.h"
#include "db_wake.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define LISTENERS 8

static db_deck_listener_t listeners[LISTENERS];
static int listener_count;
static pthread_mutex_t wake_lock = PTHREAD_MUTEX_INITIALIZER;
static db_wake_t wake;
static int lit[DB_MAX_DECKS];

void db_deck_add_listener(const db_deck_listener_t *l)
{
    if (listener_count < LISTENERS) {
        listeners[listener_count++] = *l;
    }
}

static int plugged(void)
{
    int want = getenv("DB_SIM_DECKS") ? atoi(getenv("DB_SIM_DECKS")) : 1;
    return want < 0 ? 0 : want > DB_MAX_DECKS ? DB_MAX_DECKS : want;
}

static int decks(void)
{
    static int n = -1;
    if (n < 0) {
        int max = db_config()->max_decks;
        n = plugged() < max ? plugged() : max; /* before db_deck_info asks for it */
        db_wake_init(&wake, esp_timer_get_time());
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            lit[d] = db_config()->brightness;
        }
        for (int d = 0; d < n; d++) { /* as the deck task tells them on the device */
            db_device_info_t info;
            db_deck_info(d, &info);
            for (int i = 0; i < listener_count; i++) {
                if (listeners[i].device_changed) {
                    listeners[i].device_changed(d, &info);
                }
            }
        }
    }
    return n;
}

int db_deck_over_limit(void)
{
    return plugged() - decks();
}

void db_deck_set_max(int max_decks)
{
}

int db_deck_first(void)
{
    return decks() ? 0 : -1;
}

int db_deck_find(const char *serial)
{
    for (int d = 0; d < decks(); d++) {
        db_device_info_t info;
        if (db_deck_info(d, &info) && strcmp(info.serial, serial) == 0) {
            return d;
        }
    }
    return -1;
}

bool db_deck_info(int deck, db_device_info_t *out)
{
    if (deck >= decks()) {
        return false;
    }
    *out = (db_device_info_t){.vendor = "Elgato",
                              .model = "Stream Deck +",
                              .vid = 0x0fd9,
                              .pid = 0x0084,
                              .key_count = 8,
                              .encoder_count = 4,
                              .display_count = 1};
    snprintf(out->serial, sizeof(out->serial), "SIM%04d", deck + 1);
    strcpy(out->firmware, "sim");
    out->displays[0].width = 800;
    out->displays[0].height = 480;
    return true;
}

static bool input_real;
static uint32_t input_only_ip;

bool db_deck_input_real(void)
{
    return input_real;
}

bool db_deck_input_from_deck(void)
{
    return false; /* the simulator has no deck: every input comes over the API */
}

bool db_deck_input_real_for(uint32_t peer_ip)
{
    return input_real && (!input_only_ip || input_only_ip == peer_ip);
}

size_t db_deck_input_space(void)
{
    return 64;
}

esp_err_t db_deck_simulate(int deck, const db_input_t *in, bool real, uint32_t only_ip)
{
    if (deck >= decks()) {
        return ESP_ERR_INVALID_STATE;
    }
    bool woke;
    pthread_mutex_lock(&wake_lock);
    bool swallowed = db_wake_input(&wake, deck, in, esp_timer_get_time(), &woke);
    pthread_mutex_unlock(&wake_lock);
    input_real = real;
    input_only_ip = only_ip;
    for (int i = 0; i < listener_count && !swallowed; i++) {
        if (listeners[i].input && (real || !listeners[i].external)) {
            listeners[i].input(deck, in);
        }
    }
    return ESP_OK;
}

bool db_deck_reconnect(uint32_t off_ms)
{
    return true;
}

esp_err_t db_deck_stall(int deck)
{
    return ESP_ERR_INVALID_STATE;
}
db_source_t db_deck_writer(int deck)
{
    return DB_SOURCE_UI;
}
const char *db_deck_source_name(db_source_t source)
{
    return source == DB_SOURCE_UI ? "ui" : "other";
}
esp_err_t db_deck_set_brightness(int deck, uint8_t pct)
{
    lit[deck] = pct;
    return ESP_OK;
}

int db_deck_brightness(int deck)
{
    return lit[deck];
}

void db_deck_screen_off(bool off)
{
    pthread_mutex_lock(&wake_lock);
    db_wake_set(&wake, off, esp_timer_get_time());
    pthread_mutex_unlock(&wake_lock);
}

void db_deck_screen_tick(int off_s, bool covered)
{
    bool off;
    int64_t now = esp_timer_get_time();
    decks(); /* the wake state starts there */
    pthread_mutex_lock(&wake_lock);
    if (covered) {
        db_wake_set(&wake, false, now);
    } else if (db_wake_due(&wake, off_s, false, now, &off)) {
        db_wake_set(&wake, off, now);
    }
    pthread_mutex_unlock(&wake_lock);
}

bool db_deck_screen_is_off(void)
{
    return wake.dark;
}

size_t db_deck_shots(int deck, db_source_t *writer, db_shot_t *out, size_t max)
{
    *writer = DB_SOURCE_UI;
    return 0;
}

esp_err_t db_deck_shot_copy(int deck, uint32_t id, uint8_t **data, size_t *len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_deck_shot_upright(int deck, uint32_t id, uint8_t **jpeg, size_t *len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_dock_answer(bool accept)
{
    return ESP_OK;
}
void db_dock_pairing(int seconds)
{
}
bool db_dock_undeny(uint32_t ip)
{
    return false;
}
esp_err_t db_dock_forget(const char *ip)
{
    return ESP_OK;
}

esp_err_t db_ui_snapshot(int deck, uint8_t **jpeg, size_t *len)
{
    static const uint8_t tiny[] = {0xff, 0xd8, 0xff, 0xd9}; /* SOI, EOI: enough for a JPEG reply */
    *jpeg = malloc(sizeof(tiny));
    memcpy(*jpeg, tiny, sizeof(tiny));
    *len = sizeof(tiny);
    return ESP_OK;
}

bool db_ui_menu_chord(int encoders, int keys, bool *on_encoders, int *last)
{
    *on_encoders = false;
    *last = keys - 1;
    return *last >= 1;
}

static bool menu_open;
static int64_t pass_until, reset_until, ask_until, ask_at;
static int ask_answer = -1;

static bool covered(void)
{
    int64_t now = esp_timer_get_time();
    return now < pass_until || now < reset_until || now < ask_until;
}

esp_err_t db_ui_open_menu(int deck)
{
    if (deck >= decks()) {
        return ESP_ERR_INVALID_STATE;
    }
    menu_open = true;
    return ESP_OK;
}

void db_ui_close_menu(void)
{
    menu_open = false;
    pass_until = reset_until = 0;
    if (esp_timer_get_time() < ask_until) {
        ask_answer = 0;
        ask_until = 0;
    }
}

bool db_ui_menu_open(void)
{
    return menu_open || covered();
}

int db_ui_show_password(void)
{
    pass_until = esp_timer_get_time() + 30000000;
    return decks();
}

esp_err_t db_ui_factory_reset(void)
{
    return db_config_reset();
}

int db_ui_ask_reset(void)
{
    reset_until = esp_timer_get_time() + 60000000;
    return 60;
}

void db_ui_cancel_reset(void)
{
    reset_until = 0;
}

bool db_ui_ask_login(uint32_t ip, const char *who, int seconds)
{
    ask_at = esp_timer_get_time();
    ask_until = ask_at + (int64_t)seconds * 1000000;
    ask_answer = -1;
    return true;
}

const char *db_ui_ask_companion(const char *addr, int seconds)
{
    return NULL;
}

/* DB_SIM_LOGIN_ACCEPT=1: someone at the deck accepts every request after 1 s. */
int db_ui_login_answer(void)
{
    if (ask_answer < 0 && esp_timer_get_time() < ask_until && getenv("DB_SIM_LOGIN_ACCEPT") &&
        esp_timer_get_time() - ask_at >= 1000000) {
        ask_answer = 1;
        ask_until = 0;
    }
    return ask_answer;
}

void db_ui_end_login(void)
{
    ask_until = 0;
}

esp_err_t db_ota_begin(size_t total, int slot, bool boot)
{
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t db_ota_write(const uint8_t *data, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}
esp_err_t db_ota_end(void)
{
    return ESP_ERR_INVALID_STATE;
}
void db_ota_abort(const char *why)
{
}
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)arg));
    esp_restart();
}
void db_ota_restart(uint32_t delay_ms)
{
    xTaskCreate(restart_task, "restart", 4096, (void *)(uintptr_t)delay_ms, 5, NULL);
}
esp_err_t db_ota_start_url(const char *url)
{
    return ESP_ERR_NOT_SUPPORTED;
}
int db_ota_slots(db_ota_slot_t *out, int max)
{
    return 0;
}
esp_err_t db_ota_boot_slot(int slot)
{
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t db_ota_slot_refusal(int slot)
{
    return ESP_ERR_NOT_FOUND;
}
void db_ota_web_load(uint16_t api)
{
}
esp_err_t db_ota_web_store(uint8_t *img, size_t len, uint16_t api)
{
    return ESP_ERR_NOT_SUPPORTED; /* no partition: the device tests cover it */
}
esp_err_t db_ota_web_erase(void)
{
    return ESP_OK;
}
bool db_ota_web(db_ota_web_t *out)
{
    return false;
}
esp_err_t db_ota_web_rejected(void)
{
    return ESP_OK;
}

const char *db_ota_refusal(void)
{
    return NULL;
}

bool db_ota_restarts(void)
{
    return false;
}

size_t db_ota_boot_size(bool bootloader)
{
    return 0;
}

esp_err_t db_ota_boot_read(bool bootloader, size_t offset, void *buf, size_t len)
{
    return ESP_ERR_NOT_FOUND;
}

void db_ota_boot_info(db_ota_boot_t *out)
{
    *out = (db_ota_boot_t){.partitions = 9};
}

size_t db_log_read(char *buf, size_t cap)
{
    return (size_t)snprintf(buf, cap, "simulator\n");
}

size_t db_log_since(uint64_t *pos, char *buf, size_t cap)
{
    size_t n = *pos ? 0 : db_log_read(buf, cap);
    *pos += n;
    return n;
}

size_t db_net_companions(db_net_found_t *out, size_t max)
{
    db_net_found_t sim = {.name = "Companion (sim)", .host = "127.0.0.1", .port = 16622};
    if (max) {
        out[0] = sim;
    }
    return max ? 1 : 0;
}

static const char *sim_join = "";

/* DB_SIM_VIA_AP=1: the setup AP is on and has the loopback address, so every request counts as over the AP.
 */
void db_net_status(db_net_status_t *out)
{
    *out = (db_net_status_t){.lan_link = true, .lan_up = true, .join_state = sim_join};
    strcpy(out->lan_ip, "127.0.0.1");
    if (getenv("DB_SIM_VIA_AP")) {
        out->ap_active = true;
        strcpy(out->ap_ip, "127.0.0.1");
    }
}

esp_err_t db_net_ap_stop(void)
{
    return ESP_OK;
}

int db_net_wifi_scan(db_net_ap_t *out, size_t max)
{
    db_net_ap_t sim[] = {{"Zuhause", "aa:bb:cc:00:00:01", -48, 1, false},
                         {"Zuhause", "aa:bb:cc:00:00:02", -61, 6, false},
                         {"Gast", "aa:bb:cc:00:00:03", -70, 11, true}};
    size_t n = max < 3 ? max : 3;
    memcpy(out, sim, n * sizeof(*out));
    return (int)n;
}

esp_err_t db_net_wifi_join(const char *ssid, const char *pass, const char *bssid)
{
    uint8_t mac[6];
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64 || (bssid[0] && !db_config_bssid(bssid, mac))) {
        return ESP_ERR_INVALID_ARG;
    }
    sim_join = strcmp(ssid, "Zuhause") == 0 ? "ok" : "failed"; /* no radio: a known name joins at once */
    return ESP_OK;
}

/* metrics.c reads task and image statistics the host does not have: same keys, no samples, no image. */
void metrics_start(void)
{
}

esp_err_t get_metrics(httpd_req_t *req)
{
    nvs_stats_t nvs = {0};
    nvs_get_stats(NULL, &nvs);
    char body[192];
    snprintf(
        body, sizeof(body),
        "{\"every_s\":5,\"cpu\":[],\"heap\":[],\"psram\":[],\"flash\":{\"firmware\":{\"used\":0,\"size\":%d},"
        "\"settings\":{\"used\":%u,\"size\":%u}}}",
        0x400000, (unsigned)nvs.used_entries * 32, (unsigned)nvs.total_entries * 32);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

esp_err_t get_profile(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"routes\":[]}");
}

/* IDF's esp_timer has only its header on Linux. Timers never fire: the web watchdog has nothing to watch
 * here. */
int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    *out = NULL;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    return ESP_OK;
}

/* No servers and no Satellite client in the simulator: retries find nothing to wake. */
const char *db_link_url(int index)
{
    return "";
}

void db_link_retry(int index)
{
}

const char *db_satellite_address(void)
{
    return "";
}

void db_satellite_retry(void)
{
}

void db_satellite_moved(void)
{
}

void db_log_event(const char *tag, const char *kind, const char *fmt, ...)
{
}

size_t db_log_events(db_event_t *out, size_t max)
{
    if (!max) {
        return 0;
    }
    out[0] = (db_event_t){.at_us = 0, .kind = "boot", .text = "started (sim)"};
    return 1;
}
size_t db_log_prev(char *buf, size_t cap)
{
    return 0;
}

const char *db_ota_inspect(const uint8_t *header)
{
    return memcmp(header, "DBFW", 4) == 0 ? "the simulator checks no signature" : "not a .dbfw file";
}

const char *db_satellite_probe(const char *addr, const char *pin_hex, char *version, size_t cap)
{
    return "the simulator reaches no Companion";
}

bool db_ui_login_take(const char *token)
{
    static bool used; /* the simulator has no deck to show a code: this one works once */
    bool ok = !used && strcmp(token, "0123456789abcdef") == 0;
    used |= ok;
    return ok;
}

bool db_ui_login_shown(void)
{
    const db_config_t *c =
        db_config(); /* the deck's rule: a code until setup is done, the password, the setup WLAN */
    return (!c->setup_done && !c->qr_login_off) || esp_timer_get_time() < pass_until ||
           getenv("DB_SIM_VIA_AP");
}

bool db_ui_password_shown(void)
{
    return db_ui_login_shown() || !db_config()->setup_done || menu_open;
}

void db_ui_setup_step(int step)
{
}

bool db_dock_request(char ip[16])
{
    return false; /* no host asks in the simulator */
}
