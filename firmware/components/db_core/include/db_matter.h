#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_device.h"
#include "db_json.h"

/*
 * Matter, in the Matter image only (firmware/matter): every deck is a bridged node, each key and dial press a
 * Generic Switch. Endpoint 1 is the aggregator; each slot has a block of 64 endpoints: the node, then its
 * keys, then its dials. The block stays while the slot keeps its deck and moves on when another deck takes
 * it.
 */

#define DB_MATTER_KEYS  48
#define DB_MATTER_DIALS 8
#define DB_MATTER_GENS  255

typedef struct {
    char serial[32];
    uint8_t gen;
} db_matter_slot_t;

/* The node's endpoint of `slot` (0 to DB_MAX_DECKS - 1) in block generation `gen`. */
uint16_t db_matter_ep(uint8_t gen, int slot);
/* The node's endpoint of `slot` for the deck `serial`. *moved: a different deck than
 * the last one, the block moved on. */
uint16_t db_matter_node_ep(db_matter_slot_t *slot, int index, const char *serial, bool *moved);
/* The switch a key or dial press or release goes to, 0 for any other input. */
uint16_t db_matter_input_ep(uint16_t node_ep, const db_input_t *in);

/* Generic Switch with momentary, release and long press; no multi-press, which would hold every press back */
#define DB_SW_LONG_US 600000

typedef enum {
    DB_SW_NONE,
    DB_SW_INITIAL_PRESS,
    DB_SW_LONG_PRESS,
    DB_SW_SHORT_RELEASE,
    DB_SW_LONG_RELEASE,
} db_sw_event_t;

typedef struct {
    bool down;
    bool long_sent;
    int64_t at; /* the press */
} db_sw_t;

db_sw_event_t db_sw_input(db_sw_t *sw, bool down, int64_t now);
/* The long press, once the switch was held long enough. */
db_sw_event_t db_sw_tick(db_sw_t *sw, int64_t now);
/* When db_sw_tick next has something to do, 0: never. */
int64_t db_sw_due(const db_sw_t *sw);

#define DB_MATTER_INPUTS (DB_MATTER_KEYS + DB_MATTER_DIALS)

/* The switches of one deck. A press while the bridge's own pages take the deck's input (`covered`) goes
 * nowhere; a release always counts, it ends what a controller saw. Both controls of the menu chord held at
 * once mute both until they are let go; the one pressed first is released for the controller at once. */
typedef struct {
    db_sw_t sw[DB_MATTER_INPUTS];
    uint64_t muted;
    uint64_t ending; /* muted after a controller saw the press: db_sw_deck_tick tells the release */
} db_sw_deck_t;

/* `chord`: the inputs (keys, then dials) of the menu chord, -1 when there is none. */
db_sw_event_t db_sw_deck_input(db_sw_deck_t *d, int input, bool down, bool covered, const int chord[2],
                               int64_t now);
db_sw_event_t db_sw_deck_tick(db_sw_deck_t *d, int input, int64_t now);
/* The deck is gone: per input the release a controller still waits for, and every switch let go. */
void db_sw_deck_gone(db_sw_deck_t *d, int64_t now, db_sw_event_t out[DB_MATTER_INPUTS]);

/* Who sent a press: the deck itself, /api/input or /api/ws (even with "real"), or POST /api/matter/press */
typedef enum { DB_MATTER_FROM_DECK, DB_MATTER_FROM_API, DB_MATTER_FROM_DEBUG } db_matter_from_t;
bool db_matter_takes(db_matter_from_t from);

/* The glue in firmware/matter/db_matter */
void db_matter_start(void);
/* "matter" in the status */
void db_matter_status(db_json_t *j);
/* "qr" and "code" for pairing */
void db_matter_codes(db_json_t *j);
/* Off takes effect after a restart. */
void db_matter_set_on(bool on);
/* The pairing code on every deck; returns on how many. */
int db_matter_show(void);
/* Pairing open for 15 min, for one more controller. */
bool db_matter_open_window(void);
/* A press for Matter alone (POST /api/matter/press): no other listener, host, menu or input log sees it. */
bool db_matter_debug_input(int deck, const db_input_t *ev);
/* Every controller is gone and the pairing code new after the restart that must follow. */
int db_matter_erase(void);
