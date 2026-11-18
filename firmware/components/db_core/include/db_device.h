#pragma once

#include "db_types.h"

#define DB_MAX_DECKS    4 /* behind one hub */
#define DB_MAX_DISPLAYS 2
#define DB_MAX_TARGETS  40
#define DB_MAX_TOUCH    2
#define DB_MAX_PLACES   48

typedef enum {
    DB_TARGET_FULL,  /* the whole display in one image */
    DB_TARGET_KEY,   /* one key, exact rectangle only */
    DB_TARGET_STRIP, /* an LCD strip; with `partial` any rectangle inside it */
} db_target_kind_t;

typedef struct {
    db_target_kind_t kind;
    uint8_t index; /* key index for DB_TARGET_KEY */
    bool partial;
    db_rect_t rect; /* in display coordinates */
} db_target_t;

typedef enum {
    DB_FORMAT_JPEG,
    DB_FORMAT_BMP,
} db_format_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    db_format_t format;
    uint16_t rotate; /* degrees clockwise, then flip_x and flip_y, before encoding (db_image_orient) */
    bool flip_x;
    bool flip_y;
    uint16_t max_bytes; /* largest image the display takes, 0: no limit */
    uint8_t target_count;
    db_target_t targets[DB_MAX_TARGETS];
} db_display_t;

typedef struct {
    uint8_t display;
    db_rect_t rect;
} db_touch_area_t;

/* Where Companion's own plugin for the deck puts a key or encoder. */
typedef struct {
    uint8_t row;
    uint8_t col;
    bool encoder;
    uint8_t index;
} db_place_t;

/* What the server needs to know to render for a device. Filled by the driver. */
typedef struct {
    const char *vendor;
    const char *model;
    uint16_t vid;
    uint16_t pid;
    bool tested;         /* checked end to end on real hardware */
    bool dock;           /* offered as a Network Dock to the Elgato App and Companion's Elgato plugin */
    const char *surface; /* Companion's surface id is "<surface>:<serial>" */
    char serial[32];
    char firmware[16];
    char product[32];      /* the USB product string, empty when unknown */
    uint8_t unit_info[21]; /* layout as the Elgato dock reports it in 03 1c bytes 5 to 25 */
    uint8_t key_count;
    uint8_t encoder_count;
    uint8_t display_count;
    db_display_t displays[DB_MAX_DISPLAYS];
    uint8_t touch_count;
    db_touch_area_t touch[DB_MAX_TOUCH];
    uint64_t tap_keys; /* bit per key that reports no release, so it cannot be held */
    uint8_t tap_encoders;
    uint8_t place_count; /* 0: the layout of Companion's Elgato plugin */
    db_place_t places[DB_MAX_PLACES];
} db_device_info_t;

typedef enum {
    DB_INPUT_KEY_DOWN = 1,
    DB_INPUT_KEY_UP = 2,
    DB_INPUT_ENCODER_DOWN = 3,
    DB_INPUT_ENCODER_UP = 4,
    DB_INPUT_ENCODER_TURN = 5,
    DB_INPUT_TOUCH_TAP = 6,
    DB_INPUT_TOUCH_LONG = 7,
    DB_INPUT_TOUCH_SWIPE = 8,
} db_input_kind_t;

typedef struct {
    db_input_kind_t kind;
    uint8_t index;
    uint8_t display;
    int16_t value;
    uint16_t x;
    uint16_t y;
    uint16_t x2;
    uint16_t y2;
    uint64_t t_us;
} db_input_t;

/* Returns the target that can show `rect` on `display`, or NULL. */
const db_target_t *db_device_find_target(const db_device_info_t *info, uint8_t display, db_rect_t rect);
/* Keys with a picture, numbered first. The rest of key_count (Neo sensors, Pedal) only give input. */
uint8_t db_device_image_keys(const db_device_info_t *info);

/* A deck's name in the slot list: its serial, or what db_deck_identity() makes for one without. */
typedef char db_slot_owner_t[32];

/* Clone decks share a serial or have none. An empty one, one many clones report, or one of the `n` present
 * decks in `others` has already becomes "port-<hub ports>" ("port-root" on the root port): no space, it goes
 * into URL queries undecoded. */
void db_deck_identity(db_slot_owner_t serial, const char *path, const db_slot_owner_t *others, int n);

/* The slot below `limit` for deck `id`: its own one when free. Else, alone, the lowest free one, so a
 * replaced single deck keeps the dock port; with others there, the lowest free one no other deck owns, else
 * the lowest free one. -1 when none is free. The slot belongs to `id` in `owners` (all `n`) from then on. */
int db_slot_take(db_slot_owner_t *owners, const bool *taken, int n, int limit, const char *id);
/* True when every free slot below `limit` belongs to another deck than `id`, and one is free. */
bool db_slot_others(const db_slot_owner_t *owners, const bool *taken, int limit, const char *id);

/* What to do about a deck whose output hangs. A root port cycle takes every deck: with `others` plugged in, a
 * deck that hangs again right after one waits 10 min for the next, doubling up to an hour; alone 10 s up to 5
 * min, also after a longer wait with others. A hang again within a minute leaves one restart after 10 min
 * uptime, armed again by an hour without a hang. Times in us. */
typedef enum { DB_STUCK_WAIT, DB_STUCK_CYCLE, DB_STUCK_RESTART } db_stuck_t;
typedef struct {
    int64_t last; /* the deck's last cycle, 0: none */
    int64_t backoff;
} db_stuck_state_t;
db_stuck_t db_stuck_next(db_stuck_state_t *st, int64_t now, bool may_cycle, bool others, bool *restarted);
