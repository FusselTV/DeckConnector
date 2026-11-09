#include "jpeg_port.h"

#include <stdint.h>
#include <string.h>

#include "db_image.h"
#include "db_screen.h"

#include "driver/jpeg_decode.h"
#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* The ESP32-P4 has a hardware JPEG encoder. Other chips would plug a software encoder in here. */

static const char *TAG = "jpeg";
static jpeg_encoder_handle_t encoder;
static jpeg_decoder_handle_t decoder;
static SemaphoreHandle_t lock; /* the UI and the Satellite client encode, the web server makes snapshots */

void jpeg_port_init(void)
{
    if (!lock) {
        lock = xSemaphoreCreateMutex();
    }
}

uint8_t *jpeg_port_alloc_rgb(size_t w, size_t h)
{
    jpeg_encode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER};
    size_t got = 0;
    return jpeg_alloc_encoder_mem(w * h * 3, &mem, &got);
}

static esp_err_t encode(const uint8_t *rgb, uint16_t w, uint16_t h, uint8_t quality, uint8_t **out,
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

esp_err_t jpeg_port_encode(const uint8_t *rgb, uint16_t w, uint16_t h, uint8_t quality, uint8_t **out,
                           size_t *out_len)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = encode(rgb, w, h, quality, out, out_len);
    xSemaphoreGive(lock);
    return err;
}

/* The decoder writes whole blocks: 16 px for subsampled directions, 8 otherwise. */
static uint32_t padded(uint32_t v, bool sub)
{
    uint32_t b = sub ? 16 : 8;
    return (v + b - 1) / b * b;
}

static esp_err_t decode(const uint8_t *jpg, size_t len, uint8_t *dst, uint16_t dw, uint16_t dh, uint16_t x,
                        uint16_t y)
{
    if (!decoder) {
        jpeg_decode_engine_cfg_t cfg = {.timeout_ms = 200};
        ESP_RETURN_ON_ERROR(jpeg_new_decoder_engine(&cfg, &decoder), TAG, "decoder");
    }
    jpeg_decode_picture_info_t info;
    ESP_RETURN_ON_ERROR(jpeg_decoder_get_info(jpg, (uint32_t)len, &info), TAG, "info");
    bool sub_x =
        info.sample_method == JPEG_DOWN_SAMPLING_YUV420 || info.sample_method == JPEG_DOWN_SAMPLING_YUV422;
    bool sub_y = info.sample_method == JPEG_DOWN_SAMPLING_YUV420;
    uint32_t pw = padded(info.width, sub_x);
    uint32_t ph = padded(info.height, sub_y);
    if (x >= dw || y >= dh || (uint64_t)pw * ph * 3 > SIZE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t in_cap = 0;
    size_t out_cap = 0;
    uint8_t *in = jpeg_alloc_decoder_mem(len, &in_cfg, &in_cap);
    uint8_t *out = jpeg_alloc_decoder_mem(pw * ph * 3, &out_cfg, &out_cap);
    esp_err_t err = in && out ? ESP_OK : ESP_ERR_NO_MEM;
    uint32_t n = 0;
    if (err == ESP_OK) {
        memcpy(in, jpg, len);
        jpeg_decode_cfg_t cfg = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB888,
                                 .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR};
        err = jpeg_decoder_process(decoder, &cfg, in, (uint32_t)len, out, (uint32_t)out_cap, &n);
    }
    if (err == ESP_OK &&
        db_screen_blit(dst, dw, dh, x, y, out, (size_t)pw * 3, info.width, info.height) != DB_OK) {
        err = ESP_ERR_INVALID_SIZE;
    }
    heap_caps_free(in);
    heap_caps_free(out);
    return err;
}

esp_err_t jpeg_port_decode_into(const uint8_t *jpg, size_t len, uint8_t *dst, uint16_t dw, uint16_t dh,
                                uint16_t x, uint16_t y)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = decode(jpg, len, dst, dw, dh, x, y);
    xSemaphoreGive(lock);
    return err;
}

static bool turned(const db_display_t *d)
{
    return d->rotate || d->flip_x || d->flip_y;
}

esp_err_t jpeg_port_encode_for(const db_display_t *display, const uint8_t *rgb, uint16_t w, uint16_t h,
                               uint8_t quality, uint8_t **out, size_t *out_len)
{
    uint8_t *up = NULL;
    uint16_t ow = w;
    uint16_t oh = h;
    if (turned(display)) {
        up = jpeg_port_alloc_rgb(w, h);
        if (!up) {
            return ESP_ERR_NO_MEM;
        }
        db_image_orient(display, rgb, w, h, up, false);
        db_image_upload_size(display, w, h, &ow, &oh);
    }
    const uint8_t *src = up ? up : rgb;
    esp_err_t err = ESP_OK;
    if (display->format == DB_FORMAT_BMP) {
        *out_len = db_bmp_size(ow, oh);
        *out = heap_caps_malloc(*out_len, MALLOC_CAP_SPIRAM);
        if (*out) {
            db_bmp_write(src, ow, oh, *out);
        } else {
            err = ESP_ERR_NO_MEM;
        }
    } else {
        err = jpeg_port_encode(src, ow, oh, quality, out, out_len);
    }
    heap_caps_free(up);
    return err;
}

esp_err_t jpeg_port_decode_for(const db_display_t *display, const uint8_t *data, size_t len, uint8_t *dst,
                               uint16_t dw, uint16_t dh, db_rect_t rect)
{
    if (!turned(display) && display->format == DB_FORMAT_JPEG) {
        return jpeg_port_decode_into(data, len, dst, dw, dh, rect.x, rect.y);
    }
    uint16_t ow = 0;
    uint16_t oh = 0;
    db_image_upload_size(display, rect.w, rect.h, &ow, &oh);
    uint8_t *up = heap_caps_calloc((size_t)ow * oh, 3, MALLOC_CAP_SPIRAM);
    uint8_t *px = heap_caps_malloc((size_t)rect.w * rect.h * 3, MALLOC_CAP_SPIRAM);
    esp_err_t err = up && px ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = display->format == DB_FORMAT_BMP
                  ? (db_bmp_read(data, len, ow, oh, up) == DB_OK ? ESP_OK : ESP_ERR_INVALID_ARG)
                  : jpeg_port_decode_into(data, len, up, ow, oh, 0, 0);
    }
    if (err == ESP_OK) {
        db_image_orient(display, up, rect.w, rect.h, px, true);
        if (db_screen_blit(dst, dw, dh, rect.x, rect.y, px, (size_t)rect.w * 3, rect.w, rect.h) != DB_OK) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }
    heap_caps_free(up);
    heap_caps_free(px);
    return err;
}
