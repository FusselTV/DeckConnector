#include "db_regions.h"

void db_regions_init(db_regions_t *q, size_t max_bytes)
{
    *q = (db_regions_t){.max_bytes = max_bytes};
}

static void unlink_after(db_regions_t *q, db_region_t *prev, db_region_t *r)
{
    if (prev) {
        prev->next = r->next;
    } else {
        q->head = r->next;
    }
    if (q->tail == r) {
        q->tail = prev;
    }
    q->bytes -= r->len;
    r->next = NULL;
}

db_err_t db_regions_put(db_regions_t *q, db_region_t *r, db_region_cb_t dropped, void *arg)
{
    db_region_t *prev = NULL;
    db_region_t *cur = q->head;
    while (cur) {
        db_region_t *next = cur->next;
        if (cur->display == r->display && db_rect_contains(r->rect, cur->rect)) {
            unlink_after(q, prev, cur);
            dropped(cur, arg);
        } else {
            prev = cur;
        }
        cur = next;
    }
    if (q->bytes + r->len > q->max_bytes) {
        return DB_ERR_NOMEM;
    }
    r->next = NULL;
    if (q->tail) {
        q->tail->next = r;
    } else {
        q->head = r;
    }
    q->tail = r;
    q->bytes += r->len;
    return DB_OK;
}

db_region_t *db_regions_take(db_regions_t *q)
{
    db_region_t *r = q->head;
    if (r) {
        unlink_after(q, NULL, r);
    }
    return r;
}

void db_regions_clear(db_regions_t *q, db_region_cb_t cb, void *arg)
{
    db_region_t *r;
    while ((r = db_regions_take(q)) != NULL) {
        cb(r, arg);
    }
}
