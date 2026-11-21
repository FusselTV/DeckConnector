#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_log.h"
#include "db_matter.h"
#include "db_net.h"
#include "db_serial.h"
#include "db_ui.h"
#include "db_web.h"
#include "esp_event.h"
#include "esp_private/partition_linux.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static int64_t started_us; /* esp_timer runs on the host's clock, which did not start with the simulator */

static size_t tagged(char *buf, size_t cap, const char *t)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", t);
    db_json_str(&j, "id", "02005e000001");
    db_json_str(&j, "firmware", getenv("DB_SIM_FIRMWARE") ? getenv("DB_SIM_FIRMWARE") : "sim");
    db_json_str(&j, "controller", "companion");
    db_json_bool(&j, "menu_open", db_ui_menu_open());
    db_json_bool(&j, "screen_off", db_deck_screen_is_off());
    db_json_int(&j, "brightness", db_config()->brightness);
    db_json_int(&j, "volume", db_config()->volume);
    db_json_int(&j, "screen_off_s", (int)db_config()->screen_off_s);
    db_json_int(&j, "screen_dim", db_config()->screen_dim);
    db_json_int(&j, "max_decks", db_config()->max_decks);
    db_json_bool(&j, "speaker", db_config()->speaker);
    db_json_int(&j, "api", DB_WEB_API);
    db_json_str(&j, "simulated_input", "local");
    db_json_str(&j, "name", db_config()->name);
    db_json_str(&j, "https_ca", db_web_ca_short());
    if (strcmp(t, "status") == 0) { /* as on the chip: the state leaves out what changes every second */
        db_json_int(&j, "uptime_s", (int)((esp_timer_get_time() - started_us) / 1000000));
    }
    db_json_obj_open(&j, "setup");
    db_json_bool(&j, "active", !db_config()->setup_done || db_web_setup_step());
    db_json_int(&j, "step", db_web_setup_step());
    db_json_bool(&j, "rerun", db_config()->setup_done && db_web_setup_step());
    db_json_obj_close(&j);
    db_json_obj_open(&j, "boot");
    db_json_str(&j, "reason", "power");
    db_json_null(&j, "at");
    db_json_obj_close(&j);
    db_json_arr_open(&j, "decks");
    db_device_info_t dev;
    for (int d = 0; db_deck_info(d, &dev); d++) {
        db_json_obj_open(&j, NULL);
        db_json_int(&j, "slot", d);
        db_json_int(&j, "number", d + 1);
        db_json_str(&j, "model", dev.model);
        db_json_str(&j, "serial", dev.serial);
        db_json_str(&j, "firmware", dev.firmware);
        db_json_int(&j, "keys", dev.key_count);
        db_json_int(&j, "encoders", dev.encoder_count);
        db_json_str(&j, "writer", db_deck_source_name(db_deck_writer(d)));
        db_json_bool(&j, "dock", dev.dock);
        db_json_int(&j, "brightness", db_deck_shown(d));
        db_json_bool(&j, "locked", false);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    db_json_int(&j, "decks_over_limit", db_deck_over_limit());
    db_json_arr_open(&j, "sources");
    db_json_arr_close(&j);
    db_matter_status(&j);
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
    started_us = esp_timer_get_time();
    signal(SIGPIPE, SIG_IGN); /* a browser that drops a socket mid-write would end the simulator */
    /* DB_SIM_FLASH: a flash image that outlives the simulator, partition table at 0x8000 included */
    const char *flash = getenv("DB_SIM_FLASH");
    if (flash) {
        strlcpy(esp_partition_get_file_mmap_ctrl_input()->flash_file_name, flash, PATH_MAX);
    }
    db_log_event("sim", DB_EV_STARTED, "power");
    nvs_flash_init();
    if (!flash) { /* IDF deletes its /tmp flash file only on a clean exit; tests kill the simulator. */
        unlink(esp_partition_get_file_mmap_ctrl_act()->flash_file_name);
    }
    db_log_keep_start();
    db_config_load();
    db_net_start();
    db_config_t cfg;
    db_config_copy(&cfg);
    const char *pw = getenv("DB_SIM_PASSWORD");
    strlcpy(cfg.admin_pass, pw ? pw : "simulator", sizeof(cfg.admin_pass));
    db_config_save(&cfg);
    db_matter_start();
    db_web_start(status_json, state_json);
    db_serial_start();
    for (;;) { /* the device's UI tick */
        vTaskDelay(pdMS_TO_TICKS(200));
        const db_config_t *c = db_config();
        db_deck_screen_tick((int)c->screen_off_s, c->screen_dim, c->brightness, db_ui_menu_open());
        /* DB_SIM_PRESS_FILE: once it exists, key 1 of deck 1 is pressed as at the deck and the file removed.
         * An API press is never real for a token, so the Home Assistant test needs this one. */
        const char *press = getenv("DB_SIM_PRESS_FILE");
        if (press && unlink(press) == 0) {
            db_input_t in = {.kind = DB_INPUT_KEY_DOWN};
            db_deck_simulate(0, &in, true, 0);
            in.kind = DB_INPUT_KEY_UP;
            db_deck_simulate(0, &in, true, 0);
        }
    }
}
