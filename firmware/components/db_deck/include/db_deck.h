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

#define DB_SOURCE_LINKS 4 /* = DB_MAX_SERVERS */
#define DB_SOURCE_DOCKS 4 /* = DB_DOCK_HOSTS */

/* The deck's own UI, then one source per server link, one per Network Dock host, and Companion over its
 * Satellite API. */
typedef enum {
    DB_SOURCE_UI = 0,
    DB_SOURCE_LINK0,
    DB_SOURCE_DOCK0 = DB_SOURCE_LINK0 + DB_SOURCE_LINKS,
    DB_SOURCE_SAT = DB_SOURCE_DOCK0 + DB_SOURCE_DOCKS,
    DB_SOURCE_COUNT,
} db_source_t;

typedef struct {
    /* `info` is NULL when the device was unplugged. */
    void (*device_changed)(const db_device_info_t *info);
    void (*input)(const db_input_t *ev);
    void (*raw_input)(const uint8_t *report, size_t len);
    /* A host whose presses act in the real world (Companion, the Elgato App). It gets simulated input only
     * when the request says a person sent it: a test once pressed real buttons in someone's home. */
    bool external;
} db_deck_listener_t;

typedef void (*db_deck_ack_cb_t)(db_ack_result_t result, uint8_t display, uint32_t seq, uint64_t t_recv,
                                 uint64_t t_start, uint64_t t_done, void *arg);

typedef struct {
    uint32_t images;
    uint32_t superseded;
    uint32_t rejected;
    uint32_t usb_errors;
    uint32_t usb_recoveries; /* hung decks reconnected by the bridge */
    uint32_t inputs;
    uint32_t inputs_dropped;
} db_deck_stats_t;

/* full_speed: run the USB port at full speed only (config usb_speed "full"); else high speed, and full speed
 * once when no deck comes up while a USB device is there. */
esp_err_t db_deck_start(bool full_speed);
bool db_deck_full_speed(void);
bool db_deck_behind_hub(void);
void db_deck_add_listener(const db_deck_listener_t *listener);
void db_deck_set_ack_handler(db_source_t source, db_deck_ack_cb_t cb, void *arg);

/* Pending images of the old writer are dropped and acknowledged as REJECTED_ROLE. `keep_picture`: the old
 * writer's picture stays until the new one draws, when the new one has none kept. */
void db_deck_set_writer(db_source_t source, bool keep_picture);
/* Unplug and plug in the deck by switching its USB port off for `off_ms` and on again; returns at once. */
bool db_deck_reconnect(uint32_t off_ms);
/* The deck stops taking images as a hung one would, until the bridge notices and reconnects it. */
esp_err_t db_deck_stall(void);
/* Drop the kept picture of a source whose host left. */
void db_deck_forget(db_source_t source);
db_source_t db_deck_writer(void);
/* The name in the JSON. */
const char *db_deck_source_name(db_source_t source);

/* Copy of the current device description. Returns false when nothing is plugged in. */
bool db_deck_info(db_device_info_t *out);

/*
 * Queue one image. Takes ownership of `data` (heap_caps_malloc). The result always arrives through the ack
 * handler of `source`, also for rejected images. An image of DB_SOURCE_SAT while it is not the writer only
 * goes into its kept picture.
 */
void db_deck_submit(db_source_t source, uint8_t display, db_rect_t rect, uint32_t seq, uint8_t *data,
                    size_t len, uint64_t t_recv);

esp_err_t db_deck_set_brightness(uint8_t pct);
/* The brightness the deck has now, from whoever set it last; -1 before anyone did. */
int db_deck_brightness(void);

size_t db_deck_input_space(void);
/* Handle `in` as if the device had sent it. External listeners get it only when `real`; with `only_ip` set,
 * only the host at that address gets it, and only if it draws when the press goes out. */
esp_err_t db_deck_simulate(const db_input_t *in, bool real, uint32_t only_ip);
/* Inside an input listener: whether the event came from the deck or a request that a person sent it. */
bool db_deck_input_real(void);
/* Inside an input listener: pressed on the deck itself, not sent over the network. */
bool db_deck_input_from_deck(void);
/* Inside an external listener: whether the host at `peer_ip` may get this event. */
bool db_deck_input_real_for(uint32_t peer_ip);

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
/* Copy of one image as uploaded (JPEG, BMP on six-key decks) in PSRAM; free it with heap_caps_free. */
esp_err_t db_deck_shot_copy(uint32_t id, uint8_t **data, size_t *len);
/* The same image as an upright JPEG; free it with heap_caps_free. */
esp_err_t db_deck_shot_upright(uint32_t id, uint8_t **jpeg, size_t *len);
