#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_device.h"

/*
 * The screen saver's state, box-wide. The input that wakes the decks is swallowed, and so is everything on
 * that deck until each control pressed since is let go: no host sees a press or release of it. Turns of the
 * dial that woke them stay swallowed while they come less than DB_WAKE_TURN_GAP_US apart.
 */

#define DB_WAKE_TURN_GAP_US 500000

typedef struct {
    uint32_t keys, hidden_keys;
    uint8_t encoders, hidden_encoders;
    int turning; /* the encoder whose turn woke them, -1: none */
    int64_t turn_us;
} db_wake_deck_t;

typedef struct {
    bool dark;
    int64_t last_us; /* the last input, or when the decks went dark or woke */
    db_wake_deck_t deck[DB_MAX_DECKS];
} db_wake_t;

void db_wake_init(db_wake_t *w, int64_t now);
/* True: no listener may get `ev`. *woke: it ended the dark. */
bool db_wake_input(db_wake_t *w, int deck, const db_input_t *ev, int64_t now, bool *woke);
/* Starts the idle time again. False when the dark did not change. */
bool db_wake_set(db_wake_t *w, bool dark, int64_t now);
/* Since the last input; 0 while a control is held. */
int64_t db_wake_idle_us(const db_wake_t *w, int64_t now);
/* Once per tick: whether the dark should change. A question or menu on a deck (`covered`) wakes them and
 * keeps them lit; else they go dark after `off_s` idle seconds, never with 0. */
bool db_wake_due(const db_wake_t *w, int off_s, bool covered, int64_t now, bool *dark);
