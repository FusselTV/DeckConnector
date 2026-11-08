#pragma once

#include "esp_err.h"

/*
 * Improv Wi-Fi Serial on the console UART, for the browser flasher's first setup: WLAN, device name, admin
 * password. Commands that change settings are taken only while setup is not done or in the first minutes
 * after the start. Protocol: IMPROV.md next to the site brief.
 */
esp_err_t db_serial_start(void);
