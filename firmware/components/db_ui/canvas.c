#include "canvas.h"

#include <math.h>
#include <string.h>

/* The JPEG encoder reads RGB888 as B, G, R in memory. */
static void to_bytes(uint32_t color, uint8_t out[3])
{
    out[0] = (uint8_t)color;
    out[1] = (uint8_t)(color >> 8);
    out[2] = (uint8_t)(color >> 16);
}

static db_rect_t clip_to(const ui_canvas_t *c, db_rect_t r)
{
    if (r.x >= c->w || r.y >= c->h) {
        return (db_rect_t){0, 0, 0, 0};
    }
    r.w = (uint16_t)(r.x + r.w > c->w ? c->w - r.x : r.w);
    r.h = (uint16_t)(r.y + r.h > c->h ? c->h - r.y : r.h);
    return r;
}

/* One row filled pixel by pixel, the others copied from it: memcpy is far faster on PSRAM. */
void ui_fill(ui_canvas_t *c, db_rect_t r, uint32_t color)
{
    r = clip_to(c, r);
    if (!r.w || !r.h) {
        return;
    }
    uint8_t rgb[3];
    to_bytes(color, rgb);
    uint8_t *first = c->px + ((size_t)r.y * c->w + r.x) * 3;
    for (int x = 0; x < r.w; x++) {
        memcpy(first + x * 3, rgb, 3);
    }
    for (int y = 1; y < r.h; y++) {
        memcpy(first + (size_t)y * c->w * 3, first, (size_t)r.w * 3);
    }
}

static void blend_px(uint8_t *p, const uint8_t rgb[3], float a)
{
    for (int i = 0; i < 3; i++) {
        p[i] = (uint8_t)(p[i] + (rgb[i] - p[i]) * a + 0.5f);
    }
}

void ui_round(ui_canvas_t *c, db_rect_t r, int radius, uint32_t color)
{
    r = clip_to(c, r);
    radius = radius * 2 > r.w ? r.w / 2 : (radius * 2 > r.h ? r.h / 2 : radius);
    if (!r.w || !r.h) {
        return;
    }
    ui_fill(c, (db_rect_t){r.x, (uint16_t)(r.y + radius), r.w, (uint16_t)(r.h - 2 * radius)}, color);
    ui_fill(c, (db_rect_t){(uint16_t)(r.x + radius), r.y, (uint16_t)(r.w - 2 * radius), (uint16_t)radius},
            color);
    ui_fill(c,
            (db_rect_t){(uint16_t)(r.x + radius), (uint16_t)(r.y + r.h - radius),
                        (uint16_t)(r.w - 2 * radius), (uint16_t)radius},
            color);
    uint8_t rgb[3];
    to_bytes(color, rgb);
    /* corners: coverage from the distance to the corner circle, for smooth edges */
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            float fx = (float)(radius - dx) - 0.5f;
            float fy = (float)(radius - dy) - 0.5f;
            float a = (float)radius + 0.5f - sqrtf(fx * fx + fy * fy);
            if (a <= 0) {
                continue;
            }
            a = a > 1 ? 1 : a;
            int xs[2] = {r.x + dx, r.x + r.w - 1 - dx};
            int ys[2] = {r.y + dy, r.y + r.h - 1 - dy};
            for (int i = 0; i < 4; i++) {
                blend_px(c->px + ((size_t)ys[i / 2] * c->w + xs[i % 2]) * 3, rgb, a);
            }
        }
    }
}

/* Decode one UTF-8 code point; advances `*s`. Invalid bytes become '?'. */
static uint16_t next_codepoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp = '?';
    int len = 1;
    if (p[0] < 0x80) {
        cp = p[0];
    } else if ((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        len = 2;
    } else if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        len = 3;
    }
    *s += len;
    return cp > 0xffff ? '?' : (uint16_t)cp;
}

static const ui_glyph_t *glyph(const ui_font_t *f, uint16_t cp)
{
    for (uint16_t i = 0; i < f->count; i++) {
        if (f->glyphs[i].codepoint == cp) {
            return &f->glyphs[i];
        }
    }
    return cp == '?' ? NULL : glyph(f, '?');
}

int ui_text_width(const ui_font_t *f, const char *s)
{
    int w = 0;
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_codepoint(&s));
        w += g ? g->advance : 0;
    }
    return w;
}

static void blend(uint8_t *p, const uint8_t rgb[3], unsigned alpha)
{
    for (int i = 0; i < 3; i++) {
        p[i] = (uint8_t)((p[i] * (15 - alpha) + rgb[i] * alpha) / 15);
    }
}

static void draw_glyph(ui_canvas_t *c, const ui_glyph_t *g, const uint8_t *bits, db_rect_t clip, int pen_x,
                       int base_y, const uint8_t rgb[3])
{
    for (int gy = 0; gy < g->h; gy++) {
        int y = base_y + g->y + gy;
        if (y < clip.y || y >= clip.y + clip.h || y >= c->h) {
            continue;
        }
        for (int gx = 0; gx < g->w; gx++) {
            int x = pen_x + g->x + gx;
            if (x < clip.x || x >= clip.x + clip.w || x >= c->w) {
                continue;
            }
            size_t i = (size_t)gy * g->w + gx;
            unsigned alpha = (i & 1) ? bits[g->offset + i / 2] & 0x0f : bits[g->offset + i / 2] >> 4;
            if (alpha) {
                blend(c->px + ((size_t)y * c->w + x) * 3, rgb, alpha);
            }
        }
    }
}

static void draw_run(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int x, int y, const uint8_t rgb[3],
                     const char *s, const char *end)
{
    while (*s && (!end || s < end)) {
        const ui_glyph_t *g = glyph(f, next_codepoint(&s));
        if (g) {
            draw_glyph(c, g, f->bits, clip, x, y, rgb);
            x += g->advance;
        }
    }
}

void ui_text_at(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *s,
                ui_align_t align)
{
    uint8_t rgb[3];
    to_bytes(color, rgb);
    int w = ui_text_width(f, s);
    int x = align == UI_LEFT || w >= clip.w ? clip.x : clip.x + (clip.w - w) / 2;
    draw_run(c, f, clip, x, y, rgb, s, NULL);
}

void ui_text(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *s)
{
    ui_text_at(c, f, clip, y, color, s, UI_CENTER);
}

void ui_text_fit(ui_canvas_t *c, const ui_font_t *const *fonts, int n, db_rect_t clip, int y, uint32_t color,
                 const char *s, ui_align_t align)
{
    for (int i = 0; i < n; i++) {
        if (ui_text_width(fonts[i], s) <= clip.w) {
            ui_text_at(c, fonts[i], clip, y, color, s, align);
            return;
        }
    }
    /* still too wide in the smallest font: as much as fits, then "…" */
    const ui_font_t *f = fonts[n - 1];
    int room = clip.w - ui_text_width(f, "…");
    const char *end = s;
    int w = 0;
    int fits = 0;
    for (const char *p = s; *p;) {
        const ui_glyph_t *g = glyph(f, next_codepoint(&p));
        w += g ? g->advance : 0;
        if (w > room) {
            break;
        }
        end = p;
        fits = w;
    }
    uint8_t rgb[3];
    to_bytes(color, rgb);
    draw_run(c, f, clip, clip.x, y, rgb, s, end);
    draw_run(c, f, clip, clip.x + fits, y, rgb, "…", NULL);
}
