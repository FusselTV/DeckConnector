#pragma once

#include <stddef.h>
#include <stdint.h>

#include "db_types.h"
#include "font.h"

/* RGB888 drawing surface. Colours are 0xRRGGBB. */
typedef struct {
    uint16_t w;
    uint16_t h;
    uint8_t *px;
} ui_canvas_t;

void ui_fill(ui_canvas_t *c, db_rect_t r, uint32_t color);
/* Width in pixels of a UTF-8 string. */
int ui_text_width(const ui_font_t *f, const char *utf8);
/* Draw `utf8` centred in `clip` with its baseline at `y`. Text that does not fit is cut. */
void ui_text(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *utf8);
