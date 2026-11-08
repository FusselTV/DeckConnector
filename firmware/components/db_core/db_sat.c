#include "db_sat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_json.h"

/* miniz, in the ROM of the ESP32 chips; the host tests put zlib behind it. */
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src, size_t src_len, int flags);
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1

static void add_param(db_sat_msg_t *m, char *frag)
{
    if (!frag[0] || m->count >= DB_SAT_MAX_PARAMS) {
        return;
    }
    char *eq = strchr(frag, '=');
    if (eq) {
        *eq = '\0';
    }
    m->p[m->count++] = (db_sat_param_t){.key = frag, .value = eq ? eq + 1 : ""};
}

void db_sat_parse(char *line, db_sat_msg_t *out)
{
    out->cmd = line;
    out->count = 0;
    char *r = strchr(line, ' ');
    if (!r) {
        return;
    }
    *r++ = '\0';
    char *w = r; /* never ahead of r: the line shrinks by the quotes and backslashes it drops */
    char *frag = w;
    bool quoted = false;
    for (;; r++) {
        char c = *r;
        if (c == '\\') {
            if (r[1]) {
                *w++ = *++r;
            }
        } else if (c == '"') {
            quoted = !quoted;
        } else if (c == '\0' || (c == ' ' && !quoted)) {
            *w = '\0';
            add_param(out, frag);
            if (c == '\0') {
                return;
            }
            frag = ++w;
        } else {
            *w++ = c;
        }
    }
}

const char *db_sat_get(const db_sat_msg_t *m, const char *key)
{
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->p[i].key, key) == 0) {
            return m->p[i].value;
        }
    }
    return NULL;
}

size_t db_sat_controls(const db_device_info_t *dev, db_sat_control_t *out, size_t max)
{
    const db_display_t *d = &dev->displays[0];
    uint8_t cols = 0;
    int first_y = -1;
    db_rect_t strip = {0};
    for (uint8_t i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        if (t->kind == DB_TARGET_KEY) {
            first_y = first_y < 0 ? t->rect.y : first_y;
            cols = (uint8_t)(cols + (t->rect.y == first_y));
        }
        strip = t->kind == DB_TARGET_STRIP ? t->rect : strip;
    }
    if (!cols) {
        return 0;
    }
    size_t n = 0;
    for (uint8_t i = 0; i < d->target_count && n < max; i++) {
        const db_target_t *t = &d->targets[i];
        if (t->kind == DB_TARGET_KEY) {
            out[n++] = (db_sat_control_t){.kind = DB_SAT_KEY,
                                          .row = (uint8_t)(t->index / cols),
                                          .col = (uint8_t)(t->index % cols),
                                          .index = t->index,
                                          .rect = t->rect};
        }
    }
    uint8_t row = (uint8_t)((dev->key_count + cols - 1) / cols);
    if (strip.w) {
        uint8_t parts = dev->encoder_count ? dev->encoder_count : 1;
        uint16_t pw = (uint16_t)(strip.w / parts);
        for (uint8_t i = 0; i < parts && n < max; i++) {
            out[n++] = (db_sat_control_t){.kind = DB_SAT_LCD,
                                          .row = row,
                                          .col = i,
                                          .index = i,
                                          .rect = {(uint16_t)(strip.x + i * pw), strip.y, pw, strip.h}};
        }
        row++;
    }
    for (uint8_t i = 0; i < dev->encoder_count && n < max; i++) {
        out[n++] = (db_sat_control_t){.kind = DB_SAT_ENCODER, .row = row, .col = i, .index = i};
    }
    return n;
}

static void bitmap_preset(db_json_t *j, const char *name, db_rect_t r)
{
    db_json_obj_open(j, name);
    db_json_obj_open(j, "bitmap");
    db_json_int(j, "w", r.w);
    db_json_int(j, "h", r.h);
    db_json_obj_close(j);
    db_json_obj_close(j);
}

size_t db_sat_manifest(const db_sat_control_t *c, size_t n, char *buf, size_t cap)
{
    db_rect_t key = {0};
    db_rect_t lcd = {0};
    for (size_t i = 0; i < n; i++) {
        key = c[i].kind == DB_SAT_KEY ? c[i].rect : key;
        lcd = c[i].kind == DB_SAT_LCD ? c[i].rect : lcd;
    }
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_obj_open(&j, "stylePresets");
    bitmap_preset(&j, "default", key);
    if (lcd.w) {
        bitmap_preset(&j, "lcd", lcd);
    }
    db_json_obj_open(&j, "none");
    db_json_obj_close(&j);
    db_json_obj_close(&j);
    db_json_obj_open(&j, "controls");
    for (size_t i = 0; i < n; i++) {
        char id[8];
        snprintf(id, sizeof(id), "%u/%u", c[i].row, c[i].col);
        db_json_obj_open(&j, id);
        db_json_int(&j, "row", c[i].row);
        db_json_int(&j, "column", c[i].col);
        if (c[i].kind != DB_SAT_KEY) {
            db_json_str(&j, "stylePreset", c[i].kind == DB_SAT_LCD ? "lcd" : "none");
        }
        db_json_obj_close(&j);
    }
    db_json_obj_close(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

void db_sat_addr_host(const char *addr, char *host, size_t cap)
{
    const char *p = strstr(addr, "://");
    p = p ? p + 3 : addr;
    snprintf(host, cap, "%.*s", (int)strcspn(p, ":/"), p);
}

const db_sat_control_t *db_sat_find(const db_sat_control_t *c, size_t n, const char *id)
{
    char *end;
    unsigned long row = strtoul(id, &end, 10);
    if (*end != '/') {
        return NULL;
    }
    unsigned long col = strtoul(end + 1, &end, 10);
    for (size_t i = 0; i < n && !*end; i++) {
        if (c[i].row == row && c[i].col == col) {
            return &c[i];
        }
    }
    return NULL;
}

const db_sat_control_t *db_sat_lcd_at(const db_sat_control_t *c, size_t n, uint16_t x)
{
    for (size_t i = 0; i < n; i++) {
        if (c[i].kind == DB_SAT_LCD && x >= c[i].rect.x && x < c[i].rect.x + c[i].rect.w) {
            return &c[i];
        }
    }
    return NULL;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint8_t paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) {
        return (uint8_t)a;
    }
    return (uint8_t)(pb <= pc ? b : c);
}

/* Undo the filter of one row in place. `prev` is the row above, already undone, all zero for the first. */
static db_err_t unfilter(uint8_t type, uint8_t *row, const uint8_t *prev, size_t n, size_t bpp)
{
    switch (type) {
    case 0:
        return DB_OK;
    case 1:
        for (size_t i = bpp; i < n; i++) {
            row[i] = (uint8_t)(row[i] + row[i - bpp]);
        }
        return DB_OK;
    case 2:
        for (size_t i = 0; i < n; i++) {
            row[i] = (uint8_t)(row[i] + prev[i]);
        }
        return DB_OK;
    case 3:
        for (size_t i = 0; i < n; i++) {
            row[i] = (uint8_t)(row[i] + ((i >= bpp ? row[i - bpp] : 0) + prev[i]) / 2);
        }
        return DB_OK;
    case 4:
        for (size_t i = 0; i < bpp; i++) {
            row[i] = (uint8_t)(row[i] + prev[i]); /* paeth(0, up, 0) is up */
        }
        for (size_t i = bpp; i < n; i++) {
            row[i] = (uint8_t)(row[i] + paeth(row[i - bpp], prev[i], prev[i - bpp]));
        }
        return DB_OK;
    default:
        return DB_ERR_PROTO;
    }
}

/* tinfl wants the compressed data in one piece: one IDAT chunk is the usual case, more are joined into `buf`,
 * which is big enough for any image that inflates to w x h */
static db_err_t join_idat(const uint8_t *png, size_t len, uint8_t *buf, size_t cap, const uint8_t **zdata,
                          size_t *zlen)
{
    size_t idats = 0;
    for (size_t off = 8; off + 12 <= len;) {
        size_t at = off;
        size_t clen = be32(png + at);
        if (clen > len - at - 12) {
            return DB_ERR_PROTO;
        }
        off += clen + 12;
        if (memcmp(png + at + 4, "IDAT", 4) != 0) {
            continue;
        }
        if (idats++ == 0) {
            *zdata = png + at + 8;
        } else if (*zlen + clen > cap) {
            return DB_ERR_NOMEM;
        } else {
            if (idats == 2) {
                memcpy(buf, *zdata, *zlen);
                *zdata = buf;
            }
            memcpy(buf + *zlen, png + at + 8, clen);
        }
        *zlen += clen;
    }
    return DB_OK;
}

db_err_t db_png_decode(const uint8_t *png, size_t len, uint16_t w, uint16_t h, uint8_t *bgr, uint8_t *work,
                       size_t work_cap)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (len < 8 + 12 + 13 || memcmp(png, sig, 8) != 0 || memcmp(png + 12, "IHDR", 4) != 0) {
        return DB_ERR_PROTO;
    }
    const uint8_t *ihdr = png + 16;
    if (be32(ihdr) != w || be32(ihdr + 4) != h) {
        return DB_ERR_PROTO;
    }
    if (ihdr[8] != 8 || (ihdr[9] != 2 && ihdr[9] != 6) || ihdr[12] != 0) {
        return DB_ERR_UNSUPPORTED;
    }
    size_t bpp = ihdr[9] == 6 ? 4 : 3;
    size_t stride = 1 + bpp * w;
    if ((size_t)h * stride > work_cap) {
        return DB_ERR_NOMEM;
    }
    const uint8_t *zdata = NULL;
    size_t zlen = 0;
    db_err_t joined = join_idat(png, len, bgr, (size_t)w * h * 3, &zdata, &zlen);
    if (joined != DB_OK) {
        return joined;
    }
    if (!zdata || tinfl_decompress_mem_to_mem(work, h * stride, zdata, zlen, TINFL_FLAG_PARSE_ZLIB_HEADER) !=
                      h * stride) {
        return DB_ERR_PROTO;
    }
    /* rows are unfiltered in internal RAM: byte by byte in `work`, which is in PSRAM, it takes a third longer
     */
    size_t n = stride - 1;
    uint8_t *prev = calloc(2, n);
    if (!prev) {
        return DB_ERR_NOMEM;
    }
    uint8_t *cur = prev + n;
    db_err_t err = DB_OK;
    for (size_t y = 0; y < h && err == DB_OK; y++) {
        const uint8_t *row = work + y * stride;
        memcpy(cur, row + 1, n);
        err = unfilter(row[0], cur, prev, n, bpp);
        for (size_t x = 0; x < n; x += bpp, bgr += 3) {
            bgr[0] = cur[x + 2];
            bgr[1] = cur[x + 1];
            bgr[2] = cur[x];
        }
        uint8_t *t = prev;
        prev = cur;
        cur = t;
    }
    free(prev < cur ? prev : cur);
    if (err != DB_OK) {
        return err;
    }
    return DB_OK;
}
