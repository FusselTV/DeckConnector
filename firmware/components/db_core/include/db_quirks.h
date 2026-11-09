#pragma once

#include "db_types.h"

typedef struct {
    bool block;       /* answer NAK and keep it from the deck */
    uint32_t wait_ms; /* pause after passing it on, before the next frame */
} db_quirk_t;

/* What to do with a host's verbatim frame for the deck `pid`.
 * Mini decks refuse one command and need time after a reset. Some writes and feature reports need a pause. */
db_quirk_t db_quirk(uint16_t pid, uint8_t op, const uint8_t *p, size_t len);
