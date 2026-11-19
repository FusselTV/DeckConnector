#include "db_audio.h"

esp_err_t db_audio_start(void)
{
    return ESP_OK;
}

void db_audio_play(db_sound_t sound)
{
}

esp_err_t db_audio_play_at(db_sound_t sound, int volume)
{
    return ESP_OK;
}

esp_err_t db_audio_check(int volume, int gain, db_audio_check_t *out)
{
    return ESP_ERR_NOT_SUPPORTED;
}
