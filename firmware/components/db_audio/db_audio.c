#include "db_audio.h"

#include "db_board.h"
#include "db_config.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define RATE    16000
#define LONGEST (RATE * 3) /* samples; Find is the longest sound */

static const char *TAG = "audio";

typedef struct {
    db_sound_t sound;
    int volume;
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
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &bus), TAG, "I2C");
    i2s_chan_handle_t tx;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true; /* silence, not the last block, when a sound ends early */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &tx, NULL), TAG, "I2S");
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = b->speaker.mclk_gpio,
                     .bclk = b->speaker.bclk_gpio,
                     .ws = b->speaker.ws_gpio,
                     .dout = b->speaker.dout_gpio,
                     .din = I2S_GPIO_UNUSED},
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &sc), TAG, "I2S mode");
    audio_codec_i2s_cfg_t ic = {.port = I2S_NUM_0, .tx_handle = tx};
    audio_codec_i2c_cfg_t cc2 = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus};
    es8311_codec_cfg_t ec = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&cc2),
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = (int16_t)b->speaker.pa_gpio,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec = ec.ctrl_if && ec.gpio_if ? es8311_codec_new(&ec) : NULL;
    esp_codec_dev_cfg_t dc = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = audio_codec_new_i2s_data(&ic)};
    dev = codec && dc.data_if ? esp_codec_dev_new(&dc) : NULL;
    return dev ? ESP_OK : ESP_FAIL;
}

static void audio_task(void *arg)
{
    int16_t *pcm = heap_caps_malloc(LONGEST * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    play_t p;
    while (xQueueReceive(queue, &p, portMAX_DELAY) == pdTRUE) {
        size_t n = pcm ? db_tones_render(p.sound, p.volume, RATE, pcm, LONGEST) : 0;
        if (!n) {
            continue;
        }
        if (!dev && (broken || codec_new() != ESP_OK)) {
            if (!broken) {
                ESP_LOGW(TAG, "no speaker: the codec does not answer");
            }
            broken = true;
            continue;
        }
        esp_codec_dev_sample_info_t fs = {.sample_rate = RATE, .channel = 1, .bits_per_sample = 16};
        /* opened per sound: in between the amplifier is off and does not hiss */
        if (esp_codec_dev_open(dev, &fs) == ESP_CODEC_DEV_OK) {
            esp_codec_dev_set_out_vol(dev, 85); /* the samples carry the volume setting */
            esp_codec_dev_write(dev, pcm, (int)(n * sizeof(int16_t)));
            esp_codec_dev_close(dev);
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
    play_t p = {sound, volume};
    return xQueueSend(queue, &p, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void db_audio_play(db_sound_t sound)
{
    int volume = db_config()->volume;
    if (volume) {
        db_audio_play_at(sound, volume);
    }
}
