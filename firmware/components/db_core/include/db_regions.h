#pragma once

#include "db_types.h"

/*
 * Pending images, oldest first. A new image drops every pending image it fully covers on the same display, so
 * the device only ever gets the newest content of an area and slow links never build a queue.
 * Not thread safe: the caller holds a lock.
 */

typedef struct db_region {
    struct db_region *next;
    uint8_t display;
    uint8_t source; /* who sent it, used to route the acknowledgement */
    db_rect_t rect;
    uint32_t seq;
    uint64_t t_recv;
    size_t len;
    uint8_t *data;
} db_region_t;

typedef void (*db_region_cb_t)(db_region_t *region, void *arg);

typedef struct {
    db_region_t *head;
    db_region_t *tail;
    size_t bytes;
    size_t max_bytes;
} db_regions_t;

void db_regions_init(db_regions_t *q, size_t max_bytes);

/*
 * Append `r`. Covered regions are unlinked and passed to `dropped`. Returns DB_ERR_NOMEM, without taking `r`,
 * when it does not fit into `max_bytes` even after dropping.
 */
db_err_t db_regions_put(db_regions_t *q, db_region_t *r, db_region_cb_t dropped, void *arg);

/* Remove and return the oldest region, or NULL. */
db_region_t *db_regions_take(db_regions_t *q);

/* Remove everything; each region goes to `cb`. */
void db_regions_clear(db_regions_t *q, db_region_cb_t cb, void *arg);
