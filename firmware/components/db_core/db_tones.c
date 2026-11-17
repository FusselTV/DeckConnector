#include "db_tones.h"

#include <math.h>

typedef struct {
    uint16_t hz; /* 0: silence */
    uint16_t ms;
} note_t;

/* Short and in a high register: a small speaker plays it clearly, and it is over before anyone looks up. */
static const note_t READY[] = {{1047, 90}, {0, 30}, {1568, 140}};
static const note_t CONNECTED[] = {{1319, 80}};
static const note_t LOST[] = {{1568, 90}, {0, 30}, {1047, 140}};
static const note_t PROBLEM[] = {{440, 150}, {0, 80}, {440, 150}};
static const note_t FIND[] = {{2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}, {0, 600},
                              {2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}};

static const struct {
    const char *name;
    const note_t *notes;
    size_t n;
} SOUNDS[DB_SOUND_COUNT] = {
    [DB_SOUND_READY] = {"ready", READY, sizeof(READY) / sizeof(READY[0])},
    [DB_SOUND_CONNECTED] = {"connected", CONNECTED, sizeof(CONNECTED) / sizeof(CONNECTED[0])},
    [DB_SOUND_LOST] = {"lost", LOST, sizeof(LOST) / sizeof(LOST[0])},
    [DB_SOUND_PROBLEM] = {"problem", PROBLEM, sizeof(PROBLEM) / sizeof(PROBLEM[0])},
    [DB_SOUND_FIND] = {"find", FIND, sizeof(FIND) / sizeof(FIND[0])},
};

const char *db_sound_name(db_sound_t sound)
{
    return sound < DB_SOUND_COUNT ? SOUNDS[sound].name : NULL;
}

size_t db_tones_render(db_sound_t sound, int volume, int rate, int16_t *out, size_t max)
{
    if (sound >= DB_SOUND_COUNT || volume <= 0 || rate <= 0) {
        return 0;
    }
    volume = volume > 100 ? 100 : volume;
    /* loudness follows the square: 50 sounds about half as loud as 100; half scale leaves the codec headroom
     */
    float amp = 16000.0f * (float)(volume * volume) / 10000.0f;
    int ramp = rate / 200; /* 5 ms in and out: no click */
    size_t at = 0;
    for (size_t i = 0; i < SOUNDS[sound].n; i++) {
        const note_t *n = &SOUNDS[sound].notes[i];
        int len = rate * n->ms / 1000;
        for (int k = 0; k < len && at < max; k++, at++) {
            float env = k < ramp         ? (float)k / (float)ramp
                        : len - k < ramp ? (float)(len - k) / (float)ramp
                                         : 1.0f;
            float phase = 2.0f * (float)M_PI * (float)n->hz * (float)k / (float)rate;
            out[at] = n->hz ? (int16_t)(amp * env * sinf(phase)) : 0;
        }
    }
    return at;
}
