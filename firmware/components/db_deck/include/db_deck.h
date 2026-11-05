#pragma once

#include "db_device.h"
#include "db_proto.h"
#include "db_screen.h"
#include "esp_err.h"

/*
 * The deck service owns the USB device. Exactly one source may draw at a time (the writer): the on-device UI,
 * one of the servers, or a Network Dock host, which bypasses the image queue and writes raw reports. The
 * newest picture of every source is kept, so a source that becomes the writer again shows at once.
 */

typedef enum {
    DB_SOURCE_UI = 0,
    DB_SOURCE_LINK0,
    DB_SOURCE_LINK1,
    DB_SOURCE_APP,
    DB_SOURCE_COMPANION,
    DB_SOURCE_COUNT,
} db_source_t;

typedef struct {
    /* `info` is NULL when the device was unplugged. */
    void (*device_changed)(const db_device_info_t *info);
    void (*input)(const db_input_t *ev);
    void (*raw_input)(const uint8_t *report, size_t len);
} db_deck_listener_t;

typedef void (*db_deck_ack_cb_t)(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv,
                                 uint64_t t_start, uint64_t t_done, void *arg);

typedef struct {
    uint32_t images;
    uint32_t superseded;
    uint32_t rejected;
    uint32_t usb_errors;
    uint32_t inputs;
    uint32_t inputs_dropped;
} db_deck_stats_t;

esp_err_t db_deck_start(void);
void db_deck_add_listener(const db_deck_listener_t *listener);
void db_deck_set_ack_handler(db_source_t source, db_deck_ack_cb_t cb, void *arg);

/* Pending images of the old writer are dropped and acknowledged as REJECTED_ROLE. */
void db_deck_set_writer(db_source_t source);
db_source_t db_deck_writer(void);
/* The name in the JSON. */
const char *db_deck_source_name(db_source_t source);

/* Copy of the current device description. Returns false when nothing is plugged in. */
bool db_deck_info(db_device_info_t *out);

/*
 * Queue one image. Takes ownership of `data` (heap_caps_malloc). The result always arrives through the ack
 * handler of `source`, also for rejected images.
 */
void db_deck_submit(db_source_t source, uint8_t display, db_rect_t rect, uint32_t seq, uint8_t *data,
                    size_t len, uint64_t t_recv);

esp_err_t db_deck_set_brightness(uint8_t pct);

/* Handle `in` as if the device had sent it: the writer gets it exactly like a real press. */
esp_err_t db_deck_simulate(const db_input_t *in);

/* Raw access for the dock emulation. Writes of a source that is not the writer only update its kept picture.
 */
esp_err_t db_deck_raw_write(db_source_t source, const uint8_t *report, size_t len);
esp_err_t db_deck_raw_get_feature(uint8_t *buf, size_t len, size_t *out_len);
esp_err_t db_deck_raw_set_feature(db_source_t source, const uint8_t *buf, size_t len);

void db_deck_stats(db_deck_stats_t *out);

/* What the deck shows: the images of the current writer, oldest first, without their data. Drawn in order
 * they make the picture. */
size_t db_deck_shots(db_source_t *writer, db_shot_t *out, size_t max);
/* Changes whenever the picture of db_deck_shots changes. */
static inline uint32_t db_deck_shots_key(db_source_t writer, const db_shot_t *shots, size_t n)
{
    uint32_t key = (uint32_t)writer * 2654435761u;
    for (size_t i = 0; i < n; i++) {
        key = key * 31 + shots[i].id;
    }
    return key;
}
/* Copy of one image (JPEG) in PSRAM; free it with heap_caps_free. */
esp_err_t db_deck_shot_copy(uint32_t id, uint8_t **data, size_t *len);
