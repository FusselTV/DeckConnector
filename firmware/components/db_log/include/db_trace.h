#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Profiling: a ring of 16-byte events in PSRAM, read out over /api/trace after a run. Off after every boot and
 * until POST /api/trace turns it on; while off a point costs one load and a branch. Numbers are kept by
 * tools/dbtools/trace.py, which names them: new points and counters go last.
 */
typedef enum {
    DB_TP_U1 = 1,   /* usb: input report done. id: low 32 bits of its time */
    DB_TP_E1,       /* deck: input taken from the event queue. id as U1, aux: queue depth left */
    DB_TP_E2,       /* deck: input parsed. id as U1, aux: events */
    DB_TP_L1,       /* link: INPUT queued. id: seq, aux: send queue depth */
    DB_TP_L2,       /* link: send starts. id: INPUT seq or SHOWN id, aux: message type */
    DB_TP_L2_END,   /* link: send done */
    DB_TP_B1,       /* link: frame header read. id: WebSocket opcode, aux: length, 0xffff when longer */
    DB_TP_B2,       /* link: payload read. id: FRAME id, STREAM seq or PUT slot, aux: message type */
    DB_TP_B3,       /* link: FRAME or STREAM opened. id: its id, aux: flight slot | low byte of gen << 8 */
    DB_TP_B4,       /* link: copy of a cached image starts. id: image seq, aux: bytes / 16 */
    DB_TP_B4_END,   /* link: copy done */
    DB_TP_B5,       /* deck: image submitted. id: image seq, aux: bytes / 16 */
    DB_TP_B6,       /* deck: image write starts. id: image seq */
    DB_TP_B6_END,   /* deck: image write done */
    DB_TP_B7,       /* usb: output report submitted. aux: bytes */
    DB_TP_B7_END,   /* usb: output report done */
    DB_TP_B8,       /* link: SHOWN queued. id: FRAME id or STREAM seq, aux: 1 for a stream */
    DB_TP_COUNTER,  /* the 1 Hz sampler. deck: a db_tc_t, id: the value */
    DB_TP_BENCH,    /* /api/bench measuring the cost of a point */
    DB_TP_COUNT,
} db_tp_t;

typedef enum {
    DB_TC_HEAP_FREE,
    DB_TC_HEAP_MIN,
    DB_TC_HEAP_LARGEST,
    DB_TC_PSRAM_FREE,
    DB_TC_PSRAM_MIN,
    DB_TC_PSRAM_LARGEST,
    DB_TC_RSSI,         /* dBm + 128, 0 without WLAN */
    DB_TC_LINK_FRAMES,  /* WebSocket frames the links read */
    DB_TC_LINK_IDLE,    /* link loop turns without a frame */
    DB_TC_TX_MSGS,      /* messages the link senders sent */
    DB_TC_EVENT_WAKES,  /* deck event task wake-ups */
    DB_TC_WRITER_WAKES, /* deck writer task wake-ups */
    DB_TC_WRITER_TICKS, /* driver ticks */
    DB_TC_COUNT,
} db_tc_t;

typedef struct {
    uint64_t t_us; /* esp_timer */
    uint8_t point;
    uint8_t deck; /* deck or link, the counter for DB_TP_COUNTER */
    uint16_t aux;
    uint32_t id;
} db_trace_ev_t;

_Static_assert(sizeof(db_trace_ev_t) == 16, "the host tool reads 16-byte events");

#define DB_TRACE_EVENTS (1u << 16) /* 1 MiB */

extern db_trace_ev_t *volatile db_trace_ring;
extern uint32_t db_trace_counts[DB_TC_COUNT];

void db_trace_put(uint8_t point, uint8_t deck, uint32_t id, uint16_t aux);

#define DB_TRACE(point, deck, id, aux)                                                                       \
    do {                                                                                                     \
        if (__builtin_expect(db_trace_ring != NULL, 0)) {                                                    \
            db_trace_put((point), (uint8_t)(deck), (uint32_t)(id), (uint16_t)(aux));                         \
        }                                                                                                    \
    } while (0)

#define DB_COUNT(counter)                                                                                    \
    do {                                                                                                     \
        if (__builtin_expect(db_trace_ring != NULL, 0)) {                                                    \
            __atomic_add_fetch(&db_trace_counts[counter], 1, __ATOMIC_RELAXED);                              \
        }                                                                                                    \
    } while (0)

/* Starts a new recording; false without the memory. */
bool db_trace_on(void);
/* Stops recording; what was recorded stays readable until the next db_trace_on. */
void db_trace_off(void);
/* Events ever written since the last db_trace_on; the newest DB_TRACE_EVENTS of them are kept. */
uint32_t db_trace_written(void);
/* Copies kept events, oldest first, starting `from` events after the oldest. Returns how many. */
size_t db_trace_copy(uint32_t from, db_trace_ev_t *out, size_t max);
