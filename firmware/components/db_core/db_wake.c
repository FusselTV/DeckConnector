#include "db_wake.h"

#include <string.h>

void db_wake_init(db_wake_t *w, int64_t now)
{
    memset(w, 0, sizeof(*w));
    w->cap = -1;
    w->last_us = now;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        w->deck[d].turning = -1;
    }
}

bool db_wake_input(db_wake_t *w, int deck, bool black, const db_input_t *ev, int64_t now, bool *woke)
{
    *woke = w->cap >= 0;
    black |= w->cap == 0;
    w->cap = -1;
    w->last_us = now;
    if (deck < 0 || deck >= DB_MAX_DECKS || ev->index >= 32) {
        return black;
    }
    db_wake_deck_t *d = &w->deck[deck];
    bool busy = black || d->hidden_keys || d->hidden_encoders;
    uint32_t key = 1U << ev->index;
    uint8_t enc = (uint8_t)(ev->index < 8 ? 1U << ev->index : 0);
    bool swallow = busy;
    switch (ev->kind) {
    case DB_INPUT_KEY_DOWN:
        d->keys |= key;
        d->hidden_keys |= busy ? key : 0;
        break;
    case DB_INPUT_KEY_UP: /* one held from before the dark goes through: the hosts saw it go down */
        swallow = (d->hidden_keys & key) != 0;
        d->keys &= ~key;
        d->hidden_keys &= ~key;
        break;
    case DB_INPUT_ENCODER_DOWN:
        d->encoders |= enc;
        d->hidden_encoders |= (uint8_t)(busy ? enc : 0);
        break;
    case DB_INPUT_ENCODER_UP:
        swallow = (d->hidden_encoders & enc) != 0;
        d->encoders &= (uint8_t)~enc;
        d->hidden_encoders &= (uint8_t)~enc;
        break;
    case DB_INPUT_ENCODER_TURN:
        swallow = busy || (d->turning == ev->index && now - d->turn_us < DB_WAKE_TURN_GAP_US);
        if (swallow) {
            d->turning = ev->index;
            d->turn_us = now;
        }
        break;
    default:
        break;
    }
    return swallow;
}

void db_wake_forget(db_wake_t *w, int deck)
{
    w->deck[deck] = (db_wake_deck_t){.turning = -1};
}

bool db_wake_set(db_wake_t *w, int cap, int64_t now)
{
    bool changed = w->cap != cap;
    w->cap = cap;
    w->last_us = now;
    return changed;
}

int64_t db_wake_idle_us(const db_wake_t *w, int64_t now)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (w->deck[d].keys || w->deck[d].encoders) {
            return 0;
        }
    }
    return now - w->last_us;
}

bool db_wake_due(const db_wake_t *w, int off_s, int dim, bool covered, int64_t now, int *cap)
{
    *cap = covered                                                     ? -1
           : w->cap >= 0                                               ? w->cap
           : off_s > 0 && db_wake_idle_us(w, now) >= off_s * 1000000LL ? dim
                                                                       : -1;
    return *cap != w->cap;
}

int db_wake_shown(int cap, int lit)
{
    return cap < 0 || (lit >= 0 && lit < cap) ? lit : cap;
}
