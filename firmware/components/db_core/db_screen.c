#include "db_screen.h"

#include <string.h>

#define NO_PART UINT32_MAX

static uint32_t next_id; /* shared by all screens, so ids never repeat */

void db_screen_seed_ids(uint32_t first)
{
    next_id = first;
}

void db_screen_init(db_screen_t *s, void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *))
{
    memset(s, 0, sizeof(*s));
    s->realloc_fn = realloc_fn;
    s->free_fn = free_fn;
}

static void drop(db_screen_t *s, int i)
{
    s->free_fn(s->shots[i].data);
    memmove(&s->shots[i], &s->shots[i + 1], (size_t)(s->count - i - 1) * sizeof(s->shots[0]));
    s->count--;
}

/* NOLINTNEXTLINE(readability-non-const-parameter): the shot owns `data` and frees it */
void db_screen_put(db_screen_t *s, uint8_t display, db_rect_t rect, uint8_t *data, size_t len)
{
    for (int i = s->count - 1; i >= 0; i--) {
        if (s->shots[i].display == display && db_rect_contains(rect, s->shots[i].rect)) {
            drop(s, i);
        }
    }
    if (s->count == DB_SCREEN_MAX) {
        drop(
            s,
            0); /* ponytail: oldest goes; a host drawing over DB_SCREEN_MAX small areas loses one on replay */
    }
    s->shots[s->count++] =
        (db_shot_t){.id = ++next_id, .display = display, .rect = rect, .data = data, .len = len};
}

db_err_t db_screen_feed(db_screen_t *s, const db_chunk_t *c)
{
    if (c->part == 0) {
        s->pending.display = c->display;
        s->pending.rect = c->rect;
        s->pending.len = 0;
        s->pending.next_part = 0;
    } else if (c->part != s->pending.next_part || c->display != s->pending.display ||
               !db_rect_eq(c->rect, s->pending.rect)) {
        s->pending.next_part = NO_PART; /* wait for the next first piece */
        return DB_ERR_PROTO;
    }
    if (s->pending.len + c->len > DB_SCREEN_IMAGE_MAX) { /* a host that never sends the last piece */
        s->free_fn(s->pending.data);
        s->pending.data = NULL;
        s->pending.cap = 0;
        s->pending.next_part = NO_PART;
        return DB_ERR_NOMEM;
    }
    if (s->pending.len + c->len > s->pending.cap) {
        size_t cap = s->pending.cap ? s->pending.cap * 2 : 16384;
        while (cap < s->pending.len + c->len) {
            cap *= 2;
        }
        uint8_t *p = s->realloc_fn(s->pending.data, cap);
        if (!p) {
            s->pending.next_part = NO_PART;
            return DB_ERR_NOMEM;
        }
        s->pending.data = p;
        s->pending.cap = cap;
    }
    if (c->len) {
        memcpy(s->pending.data + s->pending.len, c->data, c->len);
    }
    s->pending.len += c->len;
    s->pending.next_part++;
    if (!c->last) {
        return DB_OK;
    }
    if (s->pending.len == 0) {
        s->pending.next_part = NO_PART;
        return DB_ERR_PROTO;
    }
    /* the shot takes the buffer; the next image starts a new one */
    uint8_t *img = s->realloc_fn(s->pending.data, s->pending.len);
    db_screen_put(s, s->pending.display, s->pending.rect, img ? img : s->pending.data, s->pending.len);
    s->pending.data = NULL;
    s->pending.cap = 0;
    s->pending.next_part = NO_PART;
    return DB_OK;
}

void db_screen_clear(db_screen_t *s)
{
    while (s->count) {
        drop(s, s->count - 1);
    }
    s->free_fn(s->pending.data);
    s->pending.data = NULL;
    s->pending.cap = 0;
    s->pending.next_part = NO_PART;
}

db_err_t db_screen_blit(uint8_t *dst, uint16_t dw, uint16_t dh, uint16_t x, uint16_t y, const uint8_t *src,
                        size_t stride, uint32_t w, uint32_t h)
{
    if (x >= dw || y >= dh) {
        return DB_ERR_PROTO;
    }
    uint32_t cw = w < (uint32_t)(dw - x) ? w : (uint32_t)(dw - x);
    for (uint32_t row = 0; row < h && y + row < dh; row++) {
        memcpy(dst + ((size_t)(y + row) * dw + x) * 3, src + (size_t)row * stride, (size_t)cw * 3);
    }
    return DB_OK;
}
