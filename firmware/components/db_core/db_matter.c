#include "db_matter.h"

#include <string.h>

uint16_t db_matter_node_ep(db_matter_slot_t *slot, int index, const char *serial, bool *moved)
{
    *moved = strncmp(slot->serial, serial, sizeof(slot->serial)) != 0;
    if (*moved) {
        slot->gen = (uint8_t)((slot->gen + 1) % DB_MATTER_GENS);
        strncpy(slot->serial, serial, sizeof(slot->serial) - 1);
        slot->serial[sizeof(slot->serial) - 1] = 0;
    }
    return (uint16_t)(64 * (1 + slot->gen * DB_MAX_DECKS + index));
}

uint16_t db_matter_input_ep(uint16_t node_ep, const db_input_t *in)
{
    bool key = in->kind == DB_INPUT_KEY_DOWN || in->kind == DB_INPUT_KEY_UP;
    bool dial = in->kind == DB_INPUT_ENCODER_DOWN || in->kind == DB_INPUT_ENCODER_UP;
    if (key && in->index < DB_MATTER_KEYS) {
        return (uint16_t)(node_ep + 1 + in->index);
    }
    if (dial && in->index < DB_MATTER_DIALS) {
        return (uint16_t)(node_ep + 1 + DB_MATTER_KEYS + in->index);
    }
    return 0;
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
        if (!down) {
            d->sw[input] = (db_sw_t){0};
            d->muted &= ~bit;
        }
        return DB_SW_NONE;
    }
    if (down && covered) {
        return DB_SW_NONE;
    }
    int other = input == chord[0] ? chord[1] : input == chord[1] ? chord[0] : -1;
    if (down && other >= 0 && d->sw[other].down) {
        d->muted |= bit | 1ULL << other;
        d->sw[input].down = true;
        return DB_SW_NONE;
    }
    return db_sw_input(&d->sw[input], down, now);
}

db_sw_event_t db_sw_deck_tick(db_sw_deck_t *d, int input, int64_t now)
{
    db_sw_event_t e = db_sw_tick(&d->sw[input], now);
    return d->muted & (1ULL << input) ? DB_SW_NONE : e;
}
