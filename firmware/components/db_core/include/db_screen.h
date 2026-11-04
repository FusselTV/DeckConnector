#pragma once

#include "db_types.h"

/*
 * What one source has drawn: the newest image per area. A new image drops every older one it covers
 * completely, so the list is what the deck shows once all of it is drawn again in order.
 */

#define DB_SCREEN_MAX 48

typedef struct {
    uint32_t id; /* unique for the life of the bridge, changes with every new image */
    uint8_t display;
    db_rect_t rect;
    uint8_t *data;
    size_t len;
} db_shot_t;

/* One image arriving in pieces, as a Network Dock host sends it. */
typedef struct {
    uint8_t display;
    db_rect_t rect;
    uint16_t part; /* 0 for the first piece */
    bool last;
    const uint8_t *data;
    size_t len;
} db_chunk_t;

typedef struct {
    db_shot_t shots[DB_SCREEN_MAX];
    int count;
    void *(*realloc_fn)(void *ptr, size_t size);
    void (*free_fn)(void *ptr);
    struct {
        uint8_t display;
        db_rect_t rect;
        uint16_t next_part;
        uint8_t *data;
        size_t len;
        size_t cap;
    } pending;
} db_screen_t;

void db_screen_init(db_screen_t *s, void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *));
/* Takes ownership of `data`. */
void db_screen_put(db_screen_t *s, uint8_t display, db_rect_t rect, uint8_t *data, size_t len);
/* Collects pieces; the last one turns them into a shot. Pieces out of order are dropped with their image. */
db_err_t db_screen_feed(db_screen_t *s, const db_chunk_t *c);
void db_screen_clear(db_screen_t *s);
