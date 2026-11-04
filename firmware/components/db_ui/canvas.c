#include "canvas.h"

/* The JPEG encoder reads RGB888 as B, G, R in memory. */
static void to_bytes(uint32_t color, uint8_t out[3])
{
    out[0] = (uint8_t)color;
    out[1] = (uint8_t)(color >> 8);
    out[2] = (uint8_t)(color >> 16);
}

void ui_fill(ui_canvas_t *c, db_rect_t r, uint32_t color)
{
    uint8_t rgb[3];
    to_bytes(color, rgb);
    for (int y = r.y; y < r.y + r.h && y < c->h; y++) {
        uint8_t *p = c->px + ((size_t)y * c->w + r.x) * 3;
        for (int x = r.x; x < r.x + r.w && x < c->w; x++) {
            *p++ = rgb[0];
            *p++ = rgb[1];
            *p++ = rgb[2];
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

void ui_text(ui_canvas_t *c, const ui_font_t *f, db_rect_t clip, int y, uint32_t color, const char *s)
{
    uint8_t rgb[3];
    to_bytes(color, rgb);
    int w = ui_text_width(f, s);
    int x = clip.x + (w < clip.w ? (clip.w - w) / 2 : 0);
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_codepoint(&s));
        if (g) {
            draw_glyph(c, g, f->bits, clip, x, y, rgb);
            x += g->advance;
        }
    }
}
