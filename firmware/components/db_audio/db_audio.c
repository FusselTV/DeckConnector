#include "db_audio.h"

#include <stdlib.h>
#include <string.h>

#include "db_board.h"
#include "db_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RATE    16000
#define LONGEST (RATE * 3)  /* samples; Find is the longest sound */
#define TAIL    (RATE / 10) /* silence after each sound: close cuts what the I2S DMA still holds, 90 ms */

#define OTHER_HZ 1500

static const char *TAG = "audio";

typedef struct {
    db_audio_check_t *c;
    SemaphoreHandle_t done;
    esp_err_t err;
} job_t;

typedef struct {
    db_sound_t sound;
    int volume;
    job_t *job; /* the self-test instead of a sound */
} play_t;

static QueueHandle_t queue;
static esp_codec_dev_handle_t dev;
static bool broken; /* the codec did not answer once: its bus stays half made, no second try */

static esp_err_t codec_new(void)
{
    const db_board_t *b = db_board();
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bc = {
        .i2c_port = 0,
        .sda_io_num = b->speaker.sda_gpio,
        .scl_io_num = b->speaker.scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    gpio_config_t pa = {.pin_bit_mask = 1ULL << b->speaker.pa_gpio, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "PA");
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &bus), TAG, "I2C");
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true; /* silence, not the last block, when a sound ends early */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &tx, &rx), TAG, "I2S");
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = b->speaker.mclk_gpio,
                     .bclk = b->speaker.bclk_gpio,
                     .ws = b->speaker.ws_gpio,
                     .dout = b->speaker.dout_gpio,
                     .din = b->speaker.din_gpio},
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &sc), TAG, "I2S mode");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &sc), TAG, "I2S in");
    audio_codec_i2s_cfg_t ic = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    audio_codec_i2c_cfg_t cc2 = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus};
    es8311_codec_cfg_t ec = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&cc2),
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1, /* ours: the self-test keeps it off through a whole run */
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    const audio_codec_if_t *codec = ec.ctrl_if && ec.gpio_if ? es8311_codec_new(&ec) : NULL;
    esp_codec_dev_cfg_t dc = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec, .data_if = audio_codec_new_i2s_data(&ic)};
    dev = codec && dc.data_if ? esp_codec_dev_new(&dc) : NULL;
    return dev ? ESP_OK : ESP_FAIL;
}

static void amp(bool on)
{
    gpio_set_level(db_board()->speaker.pa_gpio, on);
}

/* Find with the board's microphone listening: the codec's ADC sends the mic left and its own DAC right. */
static esp_err_t listen(const int16_t *pcm, size_t n, int volume, bool amp_on, db_audio_check_t *c, int k)
{
    size_t frames = n + RATE / 5;
    int16_t *lr = heap_caps_calloc(frames * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!lr) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t fs = {.sample_rate = RATE, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(dev, &fs) != ESP_CODEC_DEV_OK) {
        free(lr);
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(dev, 85);
    bool ok = esp_codec_dev_set_in_gain(dev, (float)c->gain) == ESP_CODEC_DEV_OK;
    amp(amp_on);
    static int16_t out[2 * 256];
    for (size_t at = 0; at < frames && ok; at += 256) {
        size_t len = frames - at < 256 ? frames - at : 256;
        for (size_t i = 0; i < len; i++) {
            out[2 * i] = out[2 * i + 1] = at + i < n && volume ? pcm[at + i] : 0;
        }
        ok = esp_codec_dev_write(dev, out, (int)(len * 4)) == ESP_CODEC_DEV_OK &&
             esp_codec_dev_read(dev, lr + 2 * at, (int)(len * 4)) == ESP_CODEC_DEV_OK;
    }
    amp(false);
    esp_codec_dev_close(dev);
    int hz = db_tones_pitch(DB_SOUND_FIND);
    c->mic[k] = db_tones_level(lr, frames * 2, 2, hz, RATE);
    c->dac[k] = db_tones_level(lr + 1, frames * 2 - 1, 2, hz, RATE);
    if (k == 0) {
        c->other = db_tones_level(lr, frames * 2, 2, OTHER_HZ, RATE);
    }
    free(lr);
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t check(int16_t *pcm, int volume, db_audio_check_t *c)
{
    size_t n = db_tones_render(DB_SOUND_FIND, volume, RATE, pcm, LONGEST);
    esp_err_t err = listen(pcm, n, volume, true, c, 0);
    err = err ? err : listen(pcm, n, 0, true, c, 1);
    err = err ? err : listen(pcm, n, volume, false, c, 2);
    c->heard = !err && db_tones_heard(c->mic[0], c->other, c->mic[1], c->mic[2]);
    return err;
}

static bool ready(void)
{
    if (!dev && !broken && codec_new() != ESP_OK) {
        ESP_LOGW(TAG, "no speaker: the codec does not answer");
        broken = true;
    }
    return dev;
}

static void play(int16_t *pcm, size_t n)
{
    memset(pcm + n, 0, TAIL * sizeof(int16_t));
    n += TAIL;
    esp_codec_dev_sample_info_t fs = {.sample_rate = RATE, .channel = 1, .bits_per_sample = 16};
    /* opened per sound: in between the amplifier is off and does not hiss */
    if (esp_codec_dev_open(dev, &fs) == ESP_CODEC_DEV_OK) {
        esp_codec_dev_set_out_vol(dev, 85); /* the samples carry the volume setting */
        amp(true);
        esp_codec_dev_write(dev, pcm, (int)(n * sizeof(int16_t)));
        amp(false);
        esp_codec_dev_close(dev);
    }
}

static void audio_task(void *arg)
{
    int16_t *pcm = heap_caps_calloc(LONGEST + TAIL, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!pcm) {
        ESP_LOGW(TAG, "no memory for sounds");
    }
    esp_log_level_set("ES8311", ESP_LOG_WARN); /* else a line per sound in the log */
    play_t p;
    while (xQueueReceive(queue, &p, portMAX_DELAY) == pdTRUE) {
        if (p.job) {
            p.job->err = !pcm       ? ESP_ERR_NO_MEM
                         : !ready() ? ESP_ERR_INVALID_STATE
                                    : check(pcm, p.volume, p.job->c);
            xSemaphoreGive(p.job->done);
            continue;
        }
        size_t n = pcm ? db_tones_render(p.sound, p.volume, RATE, pcm, LONGEST) : 0;
        if (n && ready()) {
            play(pcm, n);
        }
    }
}

esp_err_t db_audio_start(void)
{
    if (!db_board()->speaker.present) {
        return ESP_OK;
    }
    queue = xQueueCreate(4, sizeof(play_t));
    if (!queue || xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 2, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t db_audio_play_at(db_sound_t sound, int volume)
{
    if (!queue) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    play_t p = {.sound = sound, .volume = volume};
    return xQueueSend(queue, &p, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t db_audio_check(int volume, int gain, db_audio_check_t *out)
{
    if (!queue) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *out = (db_audio_check_t){.gain = gain};
    job_t job = {.c = out, .done = xSemaphoreCreateBinary()};
    play_t p = {.sound = DB_SOUND_FIND, .volume = volume, .job = &job};
    if (!job.done) {
        return ESP_ERR_NO_MEM;
    }
    if (xQueueSend(queue, &p, 0) != pdTRUE) {
        vSemaphoreDelete(job.done);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return job.err;
}

void db_audio_play(db_sound_t sound)
{
    int volume = db_config()->volume;
    if (volume) {
        db_audio_play_at(sound, volume);
    }
}
