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

static db_sw_out_t ev(db_sw_event_t event, uint8_t count)
{
    return (db_sw_out_t){.event = event, .count = count};
}

int db_sw_input(db_sw_t *sw, bool down, int64_t now, db_sw_out_t out[2])
{
    if (down == sw->down) {
        return 0;
    }
    sw->down = down;
    sw->at = now;
    int n = 0;
    if (down) {
        sw->count++;
        out[n++] = ev(DB_SW_INITIAL_PRESS, 0);
        if (sw->count > 1) {
            out[n++] = ev(DB_SW_MULTI_ONGOING, sw->count);
        }
        return n;
    }
    if (sw->long_sent) {
        *sw = (db_sw_t){0};
        out[n++] = ev(DB_SW_LONG_RELEASE, 0);
        return n;
    }
    out[n++] = ev(DB_SW_SHORT_RELEASE, 0);
    if (sw->count >= DB_SW_MAX) {
        out[n++] = ev(DB_SW_MULTI_COMPLETE, sw->count);
        *sw = (db_sw_t){0};
    }
    return n;
}

int64_t db_sw_due(const db_sw_t *sw)
{
    if (sw->down) {
        return sw->count == 1 && !sw->long_sent ? sw->at + DB_SW_LONG_US : 0;
    }
    return sw->count ? sw->at + DB_SW_MULTI_US : 0;
}

int db_sw_tick(db_sw_t *sw, int64_t now, db_sw_out_t *out)
{
    int64_t due = db_sw_due(sw);
    if (!due || now < due) {
        return 0;
    }
    if (sw->down) {
        sw->long_sent = true;
        *out = ev(DB_SW_LONG_PRESS, 0);
        return 1;
    }
    *out = ev(DB_SW_MULTI_COMPLETE, sw->count);
    *sw = (db_sw_t){0};
    return 1;
}
