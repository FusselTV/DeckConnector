#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The few sounds the speaker plays. */
typedef enum {
    DB_SOUND_READY,     /* started and a host draws */
    DB_SOUND_CONNECTED, /* a host came back */
    DB_SOUND_LOST,      /* the host that drew is gone */
    DB_SOUND_FIND,      /* Find: which board is this */
    DB_SOUND_COUNT,
} db_sound_t;

/* `sound` as 16-bit mono samples at `rate` Hz, `volume` 0 to 100, into `out`; the number of samples, at most
 * `max`. */
size_t db_tones_render(db_sound_t sound, int volume, int rate, int16_t *out, size_t max);

/* The strength of `hz` in every `stride`th of `n` samples (Goertzel), as mean power per sample. */
float db_tones_level(const int16_t *s, size_t n, size_t stride, int hz, int rate);

/* The speaker self-test passes: the tone stands 10x over another pitch, over the same play at volume 0 and
 * with the amplifier off. */
bool db_tones_heard(float tone, float other, float silent, float amp_off);

/* The name an API uses for a sound ("ready", "find", ...), NULL for an unknown index. */
const char *db_sound_name(db_sound_t sound);
