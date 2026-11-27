#include "db_trace.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

db_trace_ev_t *volatile db_trace_ring;
uint32_t db_trace_counts[DB_TC_COUNT];
static uint32_t written;
static db_trace_ev_t *kept; /* the ring while on, and after off until the next on */

void db_trace_put(uint8_t point, uint8_t deck, uint32_t id, uint16_t aux)
{
    db_trace_ev_t *ring = db_trace_ring;
    if (!ring) {
        return;
    }
    uint32_t i = __atomic_fetch_add(&written, 1, __ATOMIC_RELAXED) & (DB_TRACE_EVENTS - 1);
    ring[i] = (db_trace_ev_t){
        .t_us = (uint64_t)esp_timer_get_time(), .point = point, .deck = deck, .aux = aux, .id = id};
}

bool db_trace_on(void)
{
    if (!kept && !(kept = heap_caps_malloc(DB_TRACE_EVENTS * sizeof(db_trace_ev_t), MALLOC_CAP_SPIRAM))) {
        return false;
    }
    db_trace_ring = NULL;
    vTaskDelay(pdMS_TO_TICKS(10)); /* a point that read the old pointer finishes its write */
    written = 0;
    memset(db_trace_counts, 0, sizeof(db_trace_counts));
    db_trace_ring = kept;
    return true;
}

/* ponytail: the ring stays allocated (1 MiB PSRAM) until the reboot, so a point preempted between its load
 * of the pointer and its write can never write into freed memory. Free it here if PSRAM ever runs short. */
void db_trace_off(void)
{
    db_trace_ring = NULL;
}

uint32_t db_trace_written(void)
{
    return kept ? __atomic_load_n(&written, __ATOMIC_RELAXED) : 0;
}

size_t db_trace_copy(uint32_t from, db_trace_ev_t *out, size_t max)
{
    uint32_t n = db_trace_written();
    uint32_t held = n < DB_TRACE_EVENTS ? n : DB_TRACE_EVENTS;
    if (!kept || from >= held) {
        return 0;
    }
    size_t take = held - from < max ? held - from : max;
    uint32_t first = n - held + from;
    for (size_t i = 0; i < take; i++) {
        out[i] = kept[(first + i) & (DB_TRACE_EVENTS - 1)];
    }
    return take;
}
