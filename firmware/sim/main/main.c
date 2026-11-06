#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_config.h"
#include "db_json.h"
#include "db_web.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static size_t tagged(char *buf, size_t cap, const char *t)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", t);
    db_json_str(&j, "firmware", "sim");
    db_json_str(&j, "controller", "companion");
    db_json_int(&j, "api", DB_WEB_API);
    db_json_str(&j, "simulated_input", "local");
    db_json_str(&j, "name", db_config()->name);
    db_json_int(&j, "uptime_s", 0);
    db_json_arr_open(&j, "decks");
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "model", "Stream Deck +");
    db_json_str(&j, "serial", "SIM0001");
    db_json_obj_close(&j);
    db_json_arr_close(&j);
    db_json_arr_open(&j, "sources");
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t status_json(char *buf, size_t cap)
{
    return tagged(buf, cap, "status");
}

static size_t state_json(char *buf, size_t cap)
{
    return tagged(buf, cap, "state");
}

/* The password comes from DB_SIM_PASSWORD, so tests know it without reading a device. */
void app_main(void)
{
    nvs_flash_init();
    esp_event_loop_create_default(); /* esp_http_server posts its events there */
    db_config_load();
    db_config_t cfg;
    db_config_copy(&cfg);
    const char *pw = getenv("DB_SIM_PASSWORD");
    strlcpy(cfg.admin_pass, pw ? pw : "simulator", sizeof(cfg.admin_pass));
    db_config_save(&cfg);
    db_web_start(status_json, state_json);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
