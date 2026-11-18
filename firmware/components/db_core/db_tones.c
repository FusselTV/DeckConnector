#include "db_tones.h"

#include <math.h>

typedef struct {
    uint16_t hz; /* 0: silence */
    uint16_t ms;
} note_t;

/* Short and in a high register: a small speaker plays it clearly, and it is over before anyone looks up. */
static const note_t ready[] = {{1047, 90}, {0, 30}, {1568, 140}};
static const note_t connected[] = {{1319, 80}};
static const note_t lost[] = {{1568, 90}, {0, 30}, {1047, 140}};
static const note_t find[] = {{2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}, {0, 600},
                              {2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}};

static const struct {
    const char *name;
    const note_t *notes;
    size_t n;
} sounds[DB_SOUND_COUNT] = {
    [DB_SOUND_READY] = {"ready", ready, sizeof(ready) / sizeof(ready[0])},
    [DB_SOUND_CONNECTED] = {"connected", connected, sizeof(connected) / sizeof(connected[0])},
    [DB_SOUND_LOST] = {"lost", lost, sizeof(lost) / sizeof(lost[0])},
    [DB_SOUND_FIND] = {"find", find, sizeof(find) / sizeof(find[0])},
};

const char *db_sound_name(db_sound_t sound)
{
    return sound < DB_SOUND_COUNT ? sounds[sound].name : NULL;
}

size_t db_tones_render(db_sound_t sound, int volume, int rate, int16_t *out, size_t max)
{
    if (sound >= DB_SOUND_COUNT || volume <= 0 || rate <= 0) {
        return 0;
    }
    volume = volume > 100 ? 100 : volume;
    /* loudness follows the square: 50 sounds about half as loud as 100; half scale leaves the codec headroom
     */
    float amp = 16000.0F * (float)(volume * volume) / 10000.0F;
    int ramp = rate / 200; /* 5 ms in and out: no click */
    size_t at = 0;
    for (size_t i = 0; i < sounds[sound].n; i++) {
        const note_t *n = &sounds[sound].notes[i];
        int len = rate * n->ms / 1000;
        for (int k = 0; k < len && at < max; k++, at++) {
            float env = k < ramp         ? (float)k / (float)ramp
                        : len - k < ramp ? (float)(len - k) / (float)ramp
                                         : 1.0F;
            float phase = 2.0F * (float)M_PI * (float)n->hz * (float)k / (float)rate;
            out[at] = n->hz ? (int16_t)(amp * env * sinf(phase)) : 0;
        }
    }
    return at;
}

float db_tones_level(const int16_t *s, size_t n, size_t stride, int hz, int rate)
{
    float c = 2.0F * cosf(2.0F * (float)M_PI * (float)hz / (float)rate);
    float s1 = 0;
    float s2 = 0;
    size_t count = 0;
    for (size_t i = 0; i < n; i += stride, count++) {
        float s0 = (float)s[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return count ? (s1 * s1 + s2 * s2 - c * s1 * s2) / (float)count : 0;
}

bool db_tones_heard(float tone, float other, float silent, float amp_off)
{
    return tone > 10 * other && tone > 10 * silent && tone > 10 * amp_off;
}
