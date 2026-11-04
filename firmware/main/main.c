/* Composition root: starts the components and connects their hooks. No logic of its own beyond that. */
#include <stdio.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_ui.h"
#include "db_web.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define SETUP_AP_AFTER_S 60

static const char *TAG = "main";

/* Shared body of the server `status` message and /api/status. */
static void write_status(db_json_t *j)
{
    db_net_status_t n;
    db_net_status(&n);
    db_deck_stats_t st;
    db_deck_stats(&st);
    static db_device_info_t dev; /* too big for the callers' stacks */
    bool present = db_deck_info(&dev);
    const char *c6 = db_ota_c6_version();
    db_json_str(j, "name", db_config()->name);
    db_json_str(j, "fw", esp_app_get_description()->version);
    if (c6) {
        db_json_str(j, "c6", c6);
    } else {
        db_json_null(j, "c6");
    }
    db_json_str(j, "mode", db_config()->mode == DB_MODE_DOCK ? "dock" : "native");
    db_json_int(j, "uptime", esp_timer_get_time() / 1000000);
    db_json_int(j, "heap", (int64_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    db_json_int(j, "psram", (int64_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    db_json_str(j, "lan", n.lan_up ? n.lan_ip : "");
    db_json_str(j, "wlan", n.wlan_up ? n.wlan_ip : "");
    db_json_int(j, "rssi", n.rssi);
    if (present) {
        db_json_obj_open(j, "device");
        db_json_str(j, "model", dev.model);
        db_json_str(j, "serial", dev.serial);
        db_json_str(j, "fw", dev.firmware);
        db_json_obj_close(j);
    } else {
        db_json_null(j, "device");
    }
    db_json_arr_open(j, "servers");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        db_json_obj_open(j, NULL);
        db_json_str(j, "url", db_link_url(i));
        db_json_bool(j, "connected", db_link_connected(i));
        db_json_bool(j, "active", db_link_active() == i);
        db_json_obj_close(j);
    }
    db_json_arr_close(j);
    db_json_int(j, "images", st.images);
    db_json_int(j, "superseded", st.superseded);
    db_json_int(j, "rejected", st.rejected);
    db_json_int(j, "usb_errors", st.usb_errors);
    db_json_int(j, "inputs", st.inputs);
}

static size_t status_json(char *buf, size_t cap, bool as_message)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    if (as_message) {
        db_json_str(&j, "t", "status");
    }
    write_status(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t link_status(char *buf, size_t cap)
{
    return status_json(buf, cap, true);
}

static size_t web_status(char *buf, size_t cap)
{
    return status_json(buf, cap, false);
}

static void on_ota_progress(const char *target, const char *state, int pct, const char *error)
{
    char buf[128];
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", "ota");
    db_json_str(&j, "target", target);
    db_json_str(&j, "state", state);
    db_json_int(&j, "pct", pct);
    if (error) {
        db_json_str(&j, "error", error);
    }
    db_json_obj_close(&j);
    if (db_json_end(&j)) {
        db_link_broadcast(buf);
    }
    bool running = strcmp(state, "error") != 0;
    db_ui_set_update(running ? pct : -1, target[0] == 'c' ? "WLAN-Chip" : "Firmware");
}

static void on_ota_requested(const char *target, const char *url)
{
    if (db_ota_start_url(url) != ESP_OK) {
        on_ota_progress(target, "error", -1, "busy");
    }
}

static bool healthy(void)
{
    return db_net_online();
}

/* A bridge without any network opens the setup WLAN by itself. */
static void setup_ap_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(SETUP_AP_AFTER_S * 1000));
    if (!db_net_online()) {
        ESP_LOGW(TAG, "no network after %d s, opening the setup WLAN", SETUP_AP_AFTER_S);
        db_net_ap_start();
    }
    vTaskDelete(NULL);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
#ifdef DB_TEST_CRASH
    abort();
#endif
    db_log_start();
    init_nvs();
    ESP_ERROR_CHECK(db_config_load());
    ESP_LOGI(TAG, "deckbridge %s, %s mode", esp_app_get_description()->version,
             db_config()->mode == DB_MODE_DOCK ? "dock" : "native");

    ESP_ERROR_CHECK(db_ui_start());
    ESP_ERROR_CHECK(db_deck_start());
    ESP_ERROR_CHECK(db_net_start());
    db_ui_set_c6_version(db_ota_c6_version());
    db_ota_set_progress_cb(on_ota_progress);

    db_link_hooks_t link = {.status_json = link_status,
                            .c6_version = db_ota_c6_version,
                            .ota_requested = on_ota_requested,
                            .active_changed = db_ui_set_active_link};
    if (db_config()->mode == DB_MODE_NATIVE) {
        ESP_ERROR_CHECK(db_link_start(&link));
    }
    db_web_hooks_t web = {.status_json = web_status};
    ESP_ERROR_CHECK(db_web_start(&web));
    db_ota_confirm_when(healthy);
    xTaskCreate(setup_ap_task, "setup_ap", 3072, NULL, 2, NULL);
}
