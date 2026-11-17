#pragma once

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
