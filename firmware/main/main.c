#include "db_config.h"
#include "db_deck.h"
#include "db_link.h"
#include "db_net.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void on_device(const db_device_info_t *info, void *arg)
{
    ESP_LOGI(TAG, "device %s", info ? info->model : "gone");
}

static void on_input(const db_input_t *ev, void *arg)
{
    ESP_LOGI(TAG, "input kind %d index %d value %d x %d y %d", ev->kind, ev->index, ev->value, ev->x, ev->y);
}

static void on_active(int index)
{
    db_deck_set_writer(index >= 0 ? (db_source_t)(DB_SOURCE_LINK0 + index) : DB_SOURCE_UI);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(db_config_load());
    db_deck_listener_t l = {.device_changed = on_device, .input = on_input};
    db_deck_add_listener(&l);
    ESP_ERROR_CHECK(db_deck_start());
    ESP_ERROR_CHECK(db_net_start());
    db_link_hooks_t hooks = {.active_changed = on_active};
    ESP_ERROR_CHECK(db_link_start(&hooks));
}
