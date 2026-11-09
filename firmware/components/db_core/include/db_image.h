#pragma once

#include "db_device.h"

/* Pixels are 3 bytes each, in the order the encoders take (B, G, R), rows top to bottom. */

/* The size `display` wants an image of w x h uploaded in: 90 and 270 swap the sides. */
void db_image_upload_size(const db_display_t *display, uint16_t w, uint16_t h, uint16_t *ow, uint16_t *oh);

/* Turn `src` (w x h) the way `display` wants it into `dst`, which holds the same number of pixels.
 * `undo` turns an uploaded image back upright. Rotation and flips are never combined on one display. */
void db_image_orient(const db_display_t *display, const uint8_t *src, uint16_t w, uint16_t h, uint8_t *dst,
                     bool undo);

/* A 24 bit BMP file of `px` (w x h). */
size_t db_bmp_size(uint16_t w, uint16_t h);
void db_bmp_write(const uint8_t *px, uint16_t w, uint16_t h, uint8_t *out);
/* Copy a db_bmp_write file of exactly w x h back into `px`. */
db_err_t db_bmp_read(const uint8_t *bmp, size_t len, uint16_t w, uint16_t h, uint8_t *px);
