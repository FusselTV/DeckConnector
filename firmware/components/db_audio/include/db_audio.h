#pragma once

#include <stdbool.h>

#include "db_tones.h"
#include "esp_err.h"
#include "esp_http_server.h"

/* The board's speaker, when it has one (db_board). Sounds play one after the other on their own task; the
 * codec and its amplifier are on only while one plays. */
esp_err_t db_audio_start(void);
/* At the volume in the settings; nothing when that is 0 or the settings say no speaker is connected. */
void db_audio_play(db_sound_t sound);
/* At `volume` (0 to 100) whatever the settings say: Find and the test button. ESP_ERR_NOT_SUPPORTED without a
 * speaker. */
esp_err_t db_audio_play_at(db_sound_t sound, int volume);

/* From a request handler: plays the body, 16 kHz mono 16-bit PCM, at the volume setting and answers once it
 * ended: at Content-Length or when the client shuts down its sending side, so an endless stream may send a
 * length it never reaches. Find, the self-test, a newer stream, the speaker setting turned off or 5 s without
 * data end it; other sounds are skipped while it plays. ESP_OK when the request is taken;
 * ESP_ERR_NOT_SUPPORTED without a speaker, ESP_ERR_NO_MEM. */
esp_err_t db_audio_stream(httpd_req_t *req);

/* The self-test: Find at `volume` heard by the board's microphone at `gain` dB, then the same at volume 0 and
 * with the amplifier off. mic and dac (the codec's copy of what it plays) hold the level at Find's pitch for
 * each, other the mic at 1500 Hz in the first. Blocks for about 6 s. ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE
 * when the codec does not answer, ESP_FAIL when it fails during a run, ESP_ERR_TIMEOUT while sounds are
 * queued. */
typedef struct {
    int gain;
    float mic[3], dac[3], other;
    bool heard;
} db_audio_check_t;
esp_err_t db_audio_check(int volume, int gain, db_audio_check_t *out);
