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

typedef enum { UI_CENTER, UI_LEFT } ui_align_t;

void ui_fill(ui_canvas_t *c, db_rect_t r, uint32_t color);
/* Filled rectangle with round, smoothed corners. */
void ui_round(ui_canvas_t *c, db_rect_t r, int radius, uint32_t color);
/* ui_round on background `bg`: clears only the corners, not all of `r`. */
void ui_round_over(ui_canvas_t *c, db_rect_t r, int radius, uint32_t color, uint32_t bg);
int ui_text_width(const ui_font_t *f, const char *utf8);
/* ui_text_width up to `end` (NULL: the whole string). */
int ui_text_width_n(const ui_font_t *f, const char *utf8, const char *end);
/* Draw `utf8` centred in `clip` with its baseline at `y`. Text that does not fit is cut. */
void ui_text(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *utf8);
void ui_text_at(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *utf8,
                ui_align_t align);
/* Texts cut with "…" so far; host_test/test_ui.c checks that no page needs it. */
extern int ui_text_cuts;
extern void (*ui_on_cut)(const char *text, int room);
/* With the first of `fonts` (largest first) it fits in; in the last one cut with "…". */
void ui_text_fit(ui_canvas_t *c, const ui_font_t *const *fonts, int n, db_rect_t clip, int y, uint32_t color,
                 const char *utf8, ui_align_t align);
