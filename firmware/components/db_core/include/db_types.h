#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_OK = 0,
    DB_ERR_ARG,
    DB_ERR_NOMEM,
    DB_ERR_PROTO,
    DB_ERR_STATE,
    DB_ERR_IO,
    DB_ERR_UNSUPPORTED,
    DB_ERR_TIMEOUT,
} db_err_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} db_rect_t;

static inline bool db_rect_eq(db_rect_t a, db_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* True when `inner` lies completely inside `outer`. */
static inline bool db_rect_contains(db_rect_t outer, db_rect_t inner)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

static inline void db_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void db_put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static inline void db_put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static inline uint16_t db_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t db_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t db_get_u64(const uint8_t *p)
{
    return (uint64_t)db_get_u32(p) | ((uint64_t)db_get_u32(p + 4) << 32);
}
