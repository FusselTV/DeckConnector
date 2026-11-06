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
#include "db_net.h"
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

esp_err_t db_deck_simulate(const db_input_t *in, bool real)
{
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

void db_dock_answer(bool accept)
{
}
void db_dock_pairing(int seconds)
{
}
void db_dock_forget(const char *ip)
{
}

esp_err_t db_ui_snapshot(uint8_t **jpeg, size_t *len)
{
    static const uint8_t tiny[] = {0xff, 0xd8, 0xff, 0xd9}; /* SOI, EOI: enough for a JPEG reply */
    *jpeg = malloc(sizeof(tiny));
    memcpy(*jpeg, tiny, sizeof(tiny));
    *len = sizeof(tiny);
    return ESP_OK;
}

bool db_ui_menu_chord(int encoders, int keys, int *last)
{
    *last = keys - 1;
    return false;
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

size_t db_log_read(char *buf, size_t cap)
{
    return (size_t)snprintf(buf, cap, "simulator\n");
}

void db_net_status(db_net_status_t *out)
{
    *out = (db_net_status_t){.lan_link = true, .lan_up = true};
    strcpy(out->lan_ip, "127.0.0.1");
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
