#pragma once

#include <stdint.h>

typedef struct {
    uint16_t codepoint;
    uint8_t w;
    uint8_t h;
    int8_t x; /* offset from the pen position to the bitmap's left edge */
    int8_t y; /* offset from the baseline to the bitmap's top edge, negative is up */
    uint8_t advance;
    uint32_t offset; /* into `bits`, two 4 bit pixels per byte */
} ui_glyph_t;

typedef struct {
    uint16_t count;
    const ui_glyph_t *glyphs;
    const uint8_t *bits;
} ui_font_t;

extern const ui_font_t ui_font_small;
extern const ui_font_t ui_font_medium;
extern const ui_font_t ui_font_large;
