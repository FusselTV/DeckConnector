/* Matter without the stack: the switch, its state and Matter's test pairing code (passcode 20202021). */
#include <stdbool.h>

#include "db_matter.h"
#include "esp_err.h"
#include "nvs.h"

static bool on, started;

static void start(void)
{
    started |= on;
}

void db_matter_start(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(DB_MATTER_NVS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "on", &v);
        nvs_close(h);
    }
    on = v;
    start();
}

void db_matter_status(db_json_t *j)
{
    db_json_obj_open(j, "matter");
    db_json_bool(j, "on", on);
    db_json_bool(j, "running", started);
    db_json_int(j, "controllers", 0);
    db_json_bool(j, "pairing", started);
    db_json_obj_close(j);
}

bool db_matter_codes(db_json_t *j)
{
    if (!started) {
        return false;
    }
    db_json_str(j, "qr", "MT:-24J0AFN00KA0648G00");
    db_json_str(j, "code", "34970112332");
    db_json_str(j, "qr_bits",
                "21:7fd33f08097615ddaea9dbf5748380e05ff50770004f2597f251cfc3958dc94b3dcd01b6dd1f290bc2e15"
                "d6ea1eb55754d8ea092f357a600");
    return true;
}

int db_matter_set_on(bool want)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(DB_MATTER_NVS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "on", want);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err == ESP_OK) {
        on = want;
        start();
    }
    return err;
}

int db_matter_show(void)
{
    return 0;
}

bool db_matter_open_window(void)
{
    return started;
}

bool db_matter_debug_input(int deck, const db_input_t *ev)
{
    return started;
}

int db_matter_erase(void)
{
    return ESP_OK;
}
