#include "db_image.h"

#include <string.h>

#define BMP_HEADER 54

void db_image_upload_size(const db_display_t *display, uint16_t w, uint16_t h, uint16_t *ow, uint16_t *oh)
{
    bool swap = display->rotate == 90 || display->rotate == 270;
    *ow = swap ? h : w;
    *oh = swap ? w : h;
}

void db_image_orient(const db_display_t *display, const uint8_t *src, uint16_t w, uint16_t h, uint8_t *dst,
                     bool undo)
{
    uint16_t ow = 0;
    uint16_t oh = 0;
    db_image_upload_size(display, w, h, &ow, &oh);
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            uint16_t dx = x;
            uint16_t dy = y;
            switch (display->rotate) {
            case 90:
                dx = (uint16_t)(h - 1 - y);
                dy = x;
                break;
            case 180:
                dx = (uint16_t)(w - 1 - x);
                dy = (uint16_t)(h - 1 - y);
                break;
            case 270:
                dx = y;
                dy = (uint16_t)(w - 1 - x);
                break;
            default:
                break;
            }
            dx = display->flip_x ? (uint16_t)(ow - 1 - dx) : dx;
            dy = display->flip_y ? (uint16_t)(oh - 1 - dy) : dy;
            size_t up = ((size_t)y * w + x) * 3;
            size_t turned = ((size_t)dy * ow + dx) * 3;
            memcpy(dst + (undo ? up : turned), src + (undo ? turned : up), 3);
        }
    }
}

static size_t row_bytes(uint16_t w)
{
    return ((size_t)w * 3 + 3) & ~(size_t)3;
}

size_t db_bmp_size(uint16_t w, uint16_t h)
{
    return BMP_HEADER + row_bytes(w) * h;
}

void db_bmp_write(const uint8_t *px, uint16_t w, uint16_t h, uint8_t *out)
{
    size_t size = db_bmp_size(w, h);
    memset(out, 0, size);
    out[0] = 'B';
    out[1] = 'M';
    db_put_u32(out + 2, (uint32_t)size);
    db_put_u32(out + 10, BMP_HEADER);
    db_put_u32(out + 14, 40);
    db_put_u32(out + 18, w);
    db_put_u32(out + 22, h);
    db_put_u16(out + 26, 1);
    db_put_u16(out + 28, 24);
    db_put_u32(out + 34, (uint32_t)(size - BMP_HEADER));
    for (uint16_t y = 0; y < h; y++) { /* bottom row first */
        memcpy(out + BMP_HEADER + row_bytes(w) * (h - 1 - y), px + (size_t)y * w * 3, (size_t)w * 3);
    }
}

db_err_t db_bmp_read(const uint8_t *bmp, size_t len, uint16_t w, uint16_t h, uint8_t *px)
{
    if (len < db_bmp_size(w, h) || bmp[0] != 'B' || bmp[1] != 'M' || db_get_u32(bmp + 18) != w ||
        db_get_u32(bmp + 22) != h || db_get_u16(bmp + 28) != 24) {
        return DB_ERR_PROTO;
    }
    size_t at = db_get_u32(bmp + 10);
    if (at > len || row_bytes(w) * h > len - at) {
        return DB_ERR_PROTO;
    }
    for (uint16_t y = 0; y < h; y++) {
        memcpy(px + (size_t)y * w * 3, bmp + at + row_bytes(w) * (h - 1 - y), (size_t)w * 3);
    }
    return DB_OK;
}
