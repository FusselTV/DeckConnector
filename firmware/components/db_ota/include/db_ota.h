#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Signed updates for the bridge (P4) and its WLAN chip (C6), see docs/ota.md. The header of a .dbfw image is
 * checked before anything is written, the payload hash before anything is switched over.
 */

typedef void (*db_ota_progress_cb_t)(const char *target, const char *state, int pct, const char *error);

void db_ota_set_progress_cb(db_ota_progress_cb_t cb);

/* One update at a time. `total` is the size of the whole .dbfw file. */
esp_err_t db_ota_begin(size_t total);
esp_err_t db_ota_write(const uint8_t *data, size_t len);
/* Verify and switch over. On success the device restarts shortly after. */
esp_err_t db_ota_end(void);
void db_ota_abort(const char *why);

/* Download and install a .dbfw file in the background. */
esp_err_t db_ota_start_url(const char *url);

/*
 * A new image must confirm itself or the bootloader goes back to the old one. This waits up to two minutes
 * for `healthy()` and then confirms, or rolls back.
 */
void db_ota_confirm_when(bool (*healthy)(void));

/* "3.0.10", or NULL when the C6 did not answer. */
const char *db_ota_c6_version(void);
