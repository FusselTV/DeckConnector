#pragma once

#include <stdbool.h>

#include "db_tones.h"
#include "esp_err.h"

/* The board's speaker, when it has one (db_board). Sounds play one after the other on their own task; the
 * codec and its amplifier are on only while one plays. */
esp_err_t db_audio_start(void);
/* At the volume in the settings; nothing when that is 0. */
void db_audio_play(db_sound_t sound);
/* At `volume` (0 to 100) whatever the settings say: Find and the test button. ESP_ERR_NOT_SUPPORTED without a
 * speaker. */
esp_err_t db_audio_play_at(db_sound_t sound, int volume);

#define DB_AUDIO_CHECK_HZ       2093 /* Find's pitch */
#define DB_AUDIO_CHECK_OTHER_HZ 1500

/* The self-test: Find at `volume` heard by the board's microphone at `gain` dB, then the same at volume 0 and
 * with the amplifier off. mic and dac (the codec's copy of what it plays) hold the level at Find's pitch for
 * each. Blocks for about 7 s. */
typedef struct {
    int gain;
    float mic[3], dac[3], other;
    bool heard;
} db_audio_check_t;
esp_err_t db_audio_check(int volume, int gain, db_audio_check_t *out);
