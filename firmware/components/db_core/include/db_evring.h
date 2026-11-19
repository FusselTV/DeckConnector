#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The event history in its own flash partition: fixed 64-byte records, event number `seq` in slot
 * `seq % slots`. A sector is erased before the ring enters it, and the one after it ahead of time, so the
 * ring keeps between (sectors - 2) and (sectors - 1) sectors of events. A record cut short by a power cut
 * fails its check and counts as missing; nothing is written over it.
 */

#define DB_EVREC_SIZE   64
#define DB_EVREC_ARGS   46
#define DB_EVRING_ERASE 4096

typedef struct {
    uint32_t seq;     /* 0xFFFFFFFF: an empty slot */
    uint32_t unix_s;  /* 0 before the clock was set */
    uint32_t since_s; /* after the start of its boot */
    uint16_t boot;    /* boot number, counted in the settings storage */
    uint8_t code;
    uint8_t kind;
    char args[DB_EVREC_ARGS]; /* strings, each ended by a NUL, as many as fit */
    uint16_t check;
} db_evrec_t;

_Static_assert(sizeof(db_evrec_t) == DB_EVREC_SIZE, "a record is one slot");

typedef struct {
    int (*read)(uint32_t off, void *buf, size_t len);
    int (*write)(uint32_t off, const void *buf, size_t len);
    int (*erase)(uint32_t off, size_t len);
    uint32_t size; /* a multiple of DB_EVRING_ERASE, at least two sectors */
} db_evflash_t;

typedef struct {
    db_evflash_t f;
    uint32_t next;   /* the lowest seq that may be written */
    uint32_t oldest; /* the lowest seq kept; 0 when empty */
    uint32_t newest; /* 0 when empty */
    uint32_t sector; /* the sector written last, or `next`'s after open; the one after it is erased */
} db_evring_t;

/* Finds the newest record and gets the next sectors ready. `buf` holds DB_EVRING_ERASE bytes. 0 or the
 * flash error. */
int db_evring_open(db_evring_t *r, const db_evflash_t *f, void *buf);
/* rec->seq must be at least r->next; seqs between stay empty. Sets the check. */
int db_evring_append(db_evring_t *r, db_evrec_t *rec);
bool db_evring_get(const db_evring_t *r, uint32_t seq, db_evrec_t *out);

/* Packs strings into args (NULL ends the list); the one that does not fit is cut, the rest dropped. */
void db_evrec_set_args(db_evrec_t *rec, const char *const *args);
/* The n-th string in args, NULL past the last. */
const char *db_evrec_arg(const db_evrec_t *rec, int n);
uint16_t db_evrec_check(const db_evrec_t *rec);
