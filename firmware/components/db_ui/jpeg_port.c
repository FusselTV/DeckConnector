#include "jpeg_port.h"

#include <string.h>

#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

/* The ESP32-P4 has a hardware JPEG encoder. Other chips would plug a software encoder in here. */

static const char *TAG = "jpeg";
static jpeg_encoder_handle_t encoder;

uint8_t *jpeg_port_alloc_rgb(size_t w, size_t h)
{
    jpeg_encode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER};
    size_t got = 0;
    return jpeg_alloc_encoder_mem(w * h * 3, &mem, &got);
}

esp_err_t jpeg_port_encode(const uint8_t *rgb, uint16_t w, uint16_t h, uint8_t quality, uint8_t **out,
                           size_t *out_len)
{
    if (!encoder) {
        jpeg_encode_engine_cfg_t cfg = {.timeout_ms = 200};
        ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&cfg, &encoder), TAG, "engine");
    }
    /* one output buffer for every image, grown when a bigger one comes: allocating 0.5 MB of DMA memory per
     * image cost more than the encoding */
    static uint8_t *buf;
    static size_t cap;
    size_t worst = (size_t)w * h * 3 / 2 + 4096;
    if (worst > cap) {
        heap_caps_free(buf);
        jpeg_encode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
        buf = jpeg_alloc_encoder_mem(worst, &mem, &cap);
        if (!buf) {
            cap = 0;
            return ESP_ERR_NO_MEM;
        }
    }
    /* 4:2:0 needs sides that are a multiple of 16; keys of 120 px are only a multiple of 8. */
    bool by16 = w % 16 == 0 && h % 16 == 0;
    jpeg_encode_cfg_t cfg = {
        .width = w,
        .height = h,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
        .sub_sample = by16 ? JPEG_DOWN_SAMPLING_YUV420 : JPEG_DOWN_SAMPLING_YUV444,
        .image_quality = quality,
    };
    uint32_t n = 0;
    esp_err_t err = jpeg_encoder_process(encoder, &cfg, rgb, (uint32_t)w * h * 3, buf, (uint32_t)cap, &n);
    if (err != ESP_OK) {
        return err;
    }
    /* The DMA buffer is large and aligned; hand the deck a tight copy. */
    *out = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!*out) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(*out, buf, n);
    *out_len = n;
    return ESP_OK;
}
