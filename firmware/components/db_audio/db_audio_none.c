#include <stdlib.h>

#include "db_audio.h"
#include "db_stream.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stream.h"

static httpd_req_t *volatile newest;

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

/* Takes as long as the board would to play it. */
static bool sim_play(void *ctx, int16_t *pcm, size_t n)
{
    if (ctx != newest) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(n / 16));
    return true;
}

static void sim_stream(void *arg)
{
    httpd_req_t *req = arg;
    size_t cap = 16000 * 2;
    int16_t *ring = malloc(cap * sizeof(int16_t));
    db_stream_io_t io = {stream_recv, sim_play, req};
    bool all = ring && db_stream_run(&io, ring, cap, req->content_len);
    stream_end(req, ring ? NULL : "no memory", all);
    free(ring);
    vTaskDelete(NULL);
}

esp_err_t db_audio_stream(httpd_req_t *req)
{
    httpd_req_t *copy;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    newest = copy;
    if (xTaskCreate(sim_stream, "stream", 4096, copy, 2, NULL) != pdPASS) {
        stream_end(copy, "busy", false);
    }
    return ESP_OK;
}
