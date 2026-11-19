#include "db_matter.h"

#include <stdio.h>
#include <string.h>

uint16_t db_matter_node_ep(db_matter_slot_t *slot, int index, const char *serial, bool *moved)
{
    *moved = strncmp(slot->serial, serial, sizeof(slot->serial)) != 0;
    if (*moved) {
        slot->gen = (uint8_t)((slot->gen + 1) % DB_MATTER_GENS);
        strncpy(slot->serial, serial, sizeof(slot->serial) - 1);
        slot->serial[sizeof(slot->serial) - 1] = 0;
    }
    return db_matter_ep(slot->gen, index);
}

bool db_matter_takes(db_matter_from_t from)
{
    return from != DB_MATTER_FROM_API;
}

uint16_t db_matter_ep(uint8_t gen, int slot)
{
    return (uint16_t)(64 * (1 + gen * DB_MAX_DECKS + slot));
}

static bool fixed(const db_matter_layout_t *l, int i)
{
    return i < l->keys || (i >= DB_MATTER_KEYS && i < DB_MATTER_KEYS + l->dials);
}

/* Which of the turns, zones and swipes switch `i` is, -1: none */
static int extra_of(const db_matter_layout_t *l, int i)
{
    if (i < 0 || i >= DB_MATTER_INPUTS || fixed(l, i)) {
        return -1;
    }
    int n = 0;
    for (int j = 0; j < i; j++) {
        n += !fixed(l, j);
    }
    int extras = 2 * l->dials + l->zones + (l->zones ? 2 : 0);
    return n < extras ? n : -1;
}

static int extra_at(const db_matter_layout_t *l, int n)
{
    for (int i = 0; i < DB_MATTER_INPUTS; i++) {
        if (extra_of(l, i) == n) {
            return i;
        }
    }
    return -1;
}

bool db_matter_used(const db_matter_layout_t *l, int input)
{
    return (input >= 0 && fixed(l, input)) || extra_of(l, input) >= 0;
}

int db_matter_input(const db_matter_layout_t *l, const db_input_t *in, db_press_t *press)
{
    *press = in->kind == DB_INPUT_KEY_UP || in->kind == DB_INPUT_ENCODER_UP ? DB_PRESS_UP : DB_PRESS_DOWN;
    switch (in->kind) {
    case DB_INPUT_KEY_DOWN:
    case DB_INPUT_KEY_UP:
        return in->index < l->keys ? in->index : -1;
    case DB_INPUT_ENCODER_DOWN:
    case DB_INPUT_ENCODER_UP:
        return in->index < l->dials ? DB_MATTER_KEYS + in->index : -1;
    case DB_INPUT_ENCODER_TURN:
        *press = DB_PRESS_SHORT;
        return in->index < l->dials && in->value ? extra_at(l, 2 * in->index + (in->value > 0)) : -1;
    case DB_INPUT_TOUCH_TAP:
    case DB_INPUT_TOUCH_LONG: {
        *press = in->kind == DB_INPUT_TOUCH_TAP ? DB_PRESS_SHORT : DB_PRESS_LONG;
        if (!l->zones || !l->strip_w) {
            return -1;
        }
        int zone = in->x * l->zones / l->strip_w;
        return extra_at(l, 2 * l->dials + (zone < l->zones ? zone : l->zones - 1));
    }
    case DB_INPUT_TOUCH_SWIPE:
        *press = DB_PRESS_SHORT;
        return l->zones && in->x2 != in->x ? extra_at(l, 2 * l->dials + l->zones + (in->x2 > in->x)) : -1;
    }
    return -1;
}

int db_matter_order(const db_matter_layout_t *l, int i)
{
    if (!db_matter_used(l, i)) {
        return 0;
    }
    if (i < l->keys) {
        return 1 + i;
    }
    return i >= DB_MATTER_KEYS && i < DB_MATTER_KEYS + l->dials ? 1 + l->keys + i - DB_MATTER_KEYS
                                                                : 1 + l->keys + l->dials + extra_of(l, i);
}

bool db_matter_label(const db_matter_layout_t *l, int i, const char *lang, char out[17])
{
    if (!db_matter_used(l, i)) {
        return false;
    }
    bool de = strcmp(lang, "de") == 0;
    int n = extra_of(l, i);
    if (n >= 2 * l->dials + l->zones) {
        bool left = n == 2 * l->dials + l->zones;
        snprintf(out, 17, "%s",
                 left ? (de ? "Wischen links" : "Swipe left") : (de ? "Wischen rechts" : "Swipe right"));
    } else if (n >= 2 * l->dials) {
        snprintf(out, 17, "%s %d", de ? "Touchfeld" : "Touch zone", (uint8_t)(1 + n - 2 * l->dials));
    } else if (n >= 0) { /* "Drehregler 1 links" does not fit */
        snprintf(out, 17, "%s %c %s", de ? "Regler" : "Dial", (char)('1' + n / 2),
                 n % 2 ? (de ? "rechts" : "right") : (de ? "links" : "left"));
    } else if (i >= DB_MATTER_KEYS) {
        snprintf(out, 17, "%s %d", de ? "Drehregler" : "Dial", (uint8_t)(1 + i - DB_MATTER_KEYS));
    } else if (l->image_keys && i >= l->image_keys) {
        snprintf(out, 17, "%s %d", de ? "Touch-Punkt" : "Touch point", (uint8_t)(1 + i - l->image_keys));
    } else {
        snprintf(out, 17, "%s %d", de ? "Taste" : "Key", (uint8_t)(1 + i));
    }
    return true;
}

db_sw_event_t db_sw_input(db_sw_t *sw, bool down, int64_t now)
{
    if (down == sw->down) {
        return DB_SW_NONE;
    }
    if (down) {
        *sw = (db_sw_t){.down = true, .at = now};
        return DB_SW_INITIAL_PRESS;
    }
    bool was_long = sw->long_sent;
    *sw = (db_sw_t){0};
    return was_long ? DB_SW_LONG_RELEASE : DB_SW_SHORT_RELEASE;
}

int64_t db_sw_due(const db_sw_t *sw)
{
    return sw->down && !sw->long_sent ? sw->at + DB_SW_LONG_US : 0;
}

db_sw_event_t db_sw_tick(db_sw_t *sw, int64_t now)
{
    int64_t due = db_sw_due(sw);
    if (!due || now < due) {
        return DB_SW_NONE;
    }
    sw->long_sent = true;
    return DB_SW_LONG_PRESS;
}

db_sw_event_t db_sw_deck_input(db_sw_deck_t *d, int input, bool down, bool covered, const int chord[2],
                               int64_t now)
{
    uint64_t bit = 1ULL << input;
    if (d->muted & bit) {
        db_sw_event_t e = DB_SW_NONE;
        if (!down) {
            e = d->ending & bit ? db_sw_input(&d->sw[input], false, now) : DB_SW_NONE;
            d->sw[input] = (db_sw_t){0};
            d->muted &= ~bit;
            d->ending &= ~bit;
        }
        return e;
    }
    if (down && covered) {
        return DB_SW_NONE;
    }
    int other = input == chord[0] ? chord[1] : input == chord[1] ? chord[0] : -1;
    if (down && other >= 0 && d->sw[other].down) {
        d->ending |= (1ULL << other) & ~d->muted;
        d->muted |= bit | 1ULL << other;
        d->sw[input].down = true;
        return DB_SW_NONE;
    }
    return db_sw_input(&d->sw[input], down, now);
}

db_sw_event_t db_sw_deck_tick(db_sw_deck_t *d, int input, int64_t now)
{
    uint64_t bit = 1ULL << input;
    if (d->ending & bit) {
        d->ending &= ~bit;
        db_sw_event_t e = db_sw_input(&d->sw[input], false, now);
        d->sw[input].down = true; /* still held: the chord's other control looks at it */
        return e;
    }
    db_sw_event_t e = db_sw_tick(&d->sw[input], now);
    return d->muted & bit ? DB_SW_NONE : e;
}

int db_sw_deck_pulse(db_sw_deck_t *d, int input, db_press_t press, bool covered, int64_t now,
                     db_sw_event_t out[3])
{
    if (covered || (d->pulsed[input] && now - d->pulsed[input] < DB_SW_PULSE_US)) {
        return 0;
    }
    d->pulsed[input] = now;
    int n = 0;
    out[n++] = DB_SW_INITIAL_PRESS;
    if (press == DB_PRESS_LONG) {
        out[n++] = DB_SW_LONG_PRESS;
    }
    out[n++] = press == DB_PRESS_LONG ? DB_SW_LONG_RELEASE : DB_SW_SHORT_RELEASE;
    return n;
}

void db_sw_deck_gone(db_sw_deck_t *d, int64_t now, db_sw_event_t out[DB_MATTER_INPUTS])
{
    static const int none[2] = {-1, -1};
    for (int i = 0; i < DB_MATTER_INPUTS; i++) {
        out[i] = db_sw_deck_input(d, i, false, false, none, now);
    }
}
