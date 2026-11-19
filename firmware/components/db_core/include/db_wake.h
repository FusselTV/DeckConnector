#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_device.h"

/*
 * The screen saver's state, box-wide: lit, dimmed or off. An input on a deck that shows nothing (off, or its
 * own brightness 0 set on the box) is swallowed, and so is everything on that deck until each control pressed
 * since is let go: no host sees a press or release of it. Turns of the dial that woke them stay swallowed
 * while they come less than DB_WAKE_TURN_GAP_US apart. A dimmed deck can be read, so its input goes through.
 */

#define DB_WAKE_TURN_GAP_US 500000
#define DB_WAKE_PCT         50 /* a deck woken from brightness 0, when the setting is 0 too */

typedef struct {
    uint64_t keys, hidden_keys;
    uint8_t encoders, hidden_encoders;
    int turning; /* the encoder whose turn woke them, -1: none */
    int64_t turn_us;
} db_wake_deck_t;

typedef struct {
    int cap;         /* -1: lit, 0: off, else dimmed to this percent */
    int64_t last_us; /* the last input, or when the cap last changed */
    db_wake_deck_t deck[DB_MAX_DECKS];
} db_wake_t;

void db_wake_init(db_wake_t *w, int64_t now);
/* True: no listener may get `ev`. `black`: this deck shows 0 by the box's own setting. *woke: it ended the
 * dim or the dark. */
bool db_wake_input(db_wake_t *w, int deck, bool black, const db_input_t *ev, int64_t now, bool *woke);
void db_wake_forget(db_wake_t *w, int deck);
/* Starts the idle time again. False when the cap did not change. */
bool db_wake_set(db_wake_t *w, int cap, int64_t now);
/* Since the last input; 0 while a control is held. */
int64_t db_wake_idle_us(const db_wake_t *w, int64_t now);
/* Once per tick: whether the cap should change. A question or menu on a deck (`covered`) wakes them and
 * keeps them lit; else they dim to `dim` (0: off) after `off_s` idle seconds (`off_s` 0: never). */
bool db_wake_due(const db_wake_t *w, int off_s, int dim, bool covered, int64_t now, int *cap);
/* What a deck at brightness `lit` (-1: not known) shows under `cap`; -1: leave it. */
int db_wake_shown(int cap, int lit);
