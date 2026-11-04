#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Buffer for RGB888 pixels that the encoder can read directly. */
uint8_t *jpeg_port_alloc_rgb(size_t w, size_t h);

/* Encode RGB888 to JPEG. `*out` is allocated with heap_caps_malloc and owned by the caller. */
esp_err_t jpeg_port_encode(const uint8_t *rgb, uint16_t w, uint16_t h, uint8_t quality, uint8_t **out,
                           size_t *out_len);
