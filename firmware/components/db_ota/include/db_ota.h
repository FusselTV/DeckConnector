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

/*
 * One update at a time. `total` is the size of the whole .dbfw file. A P4 image goes into app slot `slot`
 * (-1: the one after the running slot, never the running one). With `boot` the device starts it afterwards.
 */
esp_err_t db_ota_begin(size_t total, int slot, bool boot);
/* Why the first 144 bytes of a .dbfw file would be refused, or NULL when the upload may go ahead. */
const char *db_ota_inspect(const uint8_t *header);
esp_err_t db_ota_write(const uint8_t *data, size_t len);
/* Verify, and switch over and restart when the begin asked to boot it. */
esp_err_t db_ota_end(void);
/* db_ota_end: the bootloader or partition table may be half written. Do not power off; write it again. */
#define DB_OTA_ERR_HALF_WRITTEN 0x15ff /* in the OTA error range, unused by IDF */
/* Whether the update db_ota_end() just finished restarts the bridge (bootloader and table updates do not). */
bool db_ota_restarts(void);
/* Why the last bootloader or table was refused, NULL when it was not. */
const char *db_ota_refusal(void);
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

typedef struct {
    char label[17];
    char version[32];
    char built[32];
    const char *state; /* "valid", "pending", "invalid" or "empty" */
    bool running;
    bool next;
    int level; /* security level, -1 when unknown */
} db_ota_slot_t;

/* The app slots in order. Returns how many there are. */
int db_ota_slots(db_ota_slot_t *out, int max);

/* Start slot `slot` after a restart. Refused for an invalid image or one below the security level. */
esp_err_t db_ota_boot_slot(int slot);

/*
 * The web page from the `www` partition: a signed .dbfw image with target web. It is used only when its key,
 * hash and api_min (at most `api`) fit; otherwise the page built into the firmware is served.
 */
typedef struct {
    const uint8_t *gz; /* gzip of index.html */
    size_t len;
    const uint8_t *sha; /* SHA-256 of gz */
    const char *version;
    uint16_t api_min;
} db_ota_web_t;

void db_ota_web_load(uint16_t api);
/* Check a whole image and write it to the partition. On ESP_OK it is served from now on and owns `img`
 * (heap_caps_malloc). Callers run one at a time with db_ota_web(). */
esp_err_t db_ota_web_store(uint8_t *img, size_t len, uint16_t api);
esp_err_t db_ota_web_erase(void);
/* The page in use from the partition; false for the built-in one. */
bool db_ota_web(db_ota_web_t *out);
/* Why the page in the partition was not used at start, or ESP_OK. */
esp_err_t db_ota_web_rejected(void);

/* What sits below the app slots. Both can be replaced with POST /api/ota and a signed "bootloader" or "ptable" file. */
typedef struct {
    uint32_t bootloader_version;
    char bootloader_idf[32];
    char bootloader_built[24];
    char bootloader_sha256[65]; /* of the whole region, as on the chip */
    int partitions;
    char table_md5[33];
    char table_sha256[65];
} db_ota_boot_t;
void db_ota_boot_info(db_ota_boot_t *out);
/* The region itself, to read it back (a backup before writing a new one). 0 when it cannot be read. */
size_t db_ota_boot_size(bool bootloader);
esp_err_t db_ota_boot_read(bool bootloader, size_t offset, void *buf, size_t len);
