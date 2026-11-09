#pragma once

#include <stddef.h>
#include <stdint.h>

#include "db_device.h"
#include "esp_err.h"

void jpeg_port_init(void);

/* RGB888 buffers here hold each pixel as B, G, R in memory. */

/* Buffer for RGB888 pixels that the encoder can read directly. */
uint8_t *jpeg_port_alloc_rgb(size_t w, size_t h);

/* Encode RGB888 to JPEG. `*out` is allocated with heap_caps_malloc and owned by the caller. */
esp_err_t jpeg_port_encode(const uint8_t *rgb, uint16_t w, uint16_t h, uint8_t quality, uint8_t **out,
                           size_t *out_len);

/* Decode a JPEG into the RGB888 picture `dst` (dw x dh) with its top left corner at x, y. */
esp_err_t jpeg_port_decode_into(const uint8_t *jpg, size_t len, uint8_t *dst, uint16_t dw, uint16_t dh,
                                uint16_t x, uint16_t y);

/* An upright picture (w x h) in the format and orientation `display` takes. `*out` as for jpeg_port_encode.
 */
esp_err_t jpeg_port_encode_for(const db_display_t *display, const uint8_t *rgb, uint16_t w, uint16_t h,
                               uint8_t quality, uint8_t **out, size_t *out_len);

/* Undo jpeg_port_encode_for: draw an uploaded image upright into `dst` (dw x dh) at `rect`. */
esp_err_t jpeg_port_decode_for(const db_display_t *display, const uint8_t *data, size_t len, uint8_t *dst,
                               uint16_t dw, uint16_t dh, db_rect_t rect);
