/* Stand-ins for everything the API layer calls on the chip: one Stream Deck + that is always attached, no
 * dock hosts, no updates. Simulated input goes to the listeners, so the web socket reports it like on the
 * device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_deck.h"
#include "db_dock.h"
#include "db_log.h"
#include "db_config.h"
#include "db_link.h"
#include "db_net.h"
#include "db_satellite.h"
#include "db_ota.h"
#include "db_ui.h"
#include "esp_http_server.h"
#include "esp_timer.h"

#define LISTENERS 8

static db_deck_listener_t listeners[LISTENERS];
static int listener_count;

void db_deck_add_listener(const db_deck_listener_t *l)
{
    if (listener_count < LISTENERS) {
        listeners[listener_count++] = *l;
    }
}

bool db_deck_info(db_device_info_t *out)
{
    *out = (db_device_info_t){.vendor = "Elgato",
                              .model = "Stream Deck +",
                              .vid = 0x0fd9,
                              .pid = 0x0084,
                              .key_count = 8,
                              .encoder_count = 4,
                              .display_count = 1};
    strcpy(out->serial, "SIM0001");
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

esp_err_t db_deck_simulate(const db_input_t *in, bool real, uint32_t only_ip)
{
    input_real = real;
    input_only_ip = only_ip;
    for (int i = 0; i < listener_count; i++) {
        if (listeners[i].input && (real || !listeners[i].external)) {
            listeners[i].input(in);
        }
    }
    return ESP_OK;
}

void db_deck_reconnect(void)
{
}
db_source_t db_deck_writer(void)
{
    return DB_SOURCE_UI;
}
const char *db_deck_source_name(db_source_t source)
{
    return source == DB_SOURCE_UI ? "ui" : "other";
}
esp_err_t db_deck_set_brightness(uint8_t pct)
{
    return ESP_OK;
}

size_t db_deck_shots(db_source_t *writer, db_shot_t *out, size_t max)
{
    *writer = DB_SOURCE_UI;
    return 0;
}

esp_err_t db_deck_shot_copy(uint32_t id, uint8_t **data, size_t *len)
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
esp_err_t db_dock_forget(const char *ip)
{
    return ESP_OK;
}

esp_err_t db_ui_snapshot(uint8_t **jpeg, size_t *len)
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

esp_err_t db_ui_open_menu(void)
{
    return ESP_OK;
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

void db_net_status(db_net_status_t *out)
{
    *out = (db_net_status_t){.lan_link = true, .lan_up = true, .join_state = sim_join};
    strcpy(out->lan_ip, "127.0.0.1");
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

/* metrics.c reads task and flash statistics the host does not have. */
void metrics_start(void)
{
}

esp_err_t get_metrics(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"every_s\":5,\"cpu\":[],\"heap\":[],\"psram\":[]}");
}

esp_err_t get_profile(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"routes\":[]}");
}

/* IDF's esp_timer has only its header on Linux. */
int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
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

size_t db_log_prev(char *buf, size_t cap)
{
    return 0;
}

const char *db_ota_inspect(const uint8_t *header)
{
    return memcmp(header, "DBFW", 4) == 0 ? "the simulator checks no signature" : "not a .dbfw file";
}
