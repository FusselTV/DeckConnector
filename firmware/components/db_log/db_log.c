#include "db_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_timer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define RING_SIZE (16 * 1024)
#define MAGIC     0x64626c67

/* In LP RAM, which no cache sits in front of and no restart clears: the next boot still reads it, also after
 * a panic. HP RAM sits behind the L1 cache, and a panic loses what has not been written back. */
static RTC_NOINIT_ATTR char ring[RING_SIZE];
static RTC_NOINIT_ATTR uint64_t total; /* bytes ever written: byte p sits at ring[p % RING_SIZE] while kept */
static RTC_NOINIT_ATTR uint32_t magic;
static char *prev;
static size_t prev_len;
static vprintf_like_t serial;
static db_event_t *events; /* PSRAM ring, made in db_log_start() */
static uint32_t n_events;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static int capture(const char *fmt, va_list args)
{
    char line[192];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        portENTER_CRITICAL(&mux);
        for (size_t i = 0; i < len; i++) {
            ring[total++ % RING_SIZE] = line[i];
        }
        portEXIT_CRITICAL(&mux);
    }
    return serial(fmt, args);
}

void db_log_start(void)
{
    if (magic == MAGIC && total && (prev = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM))) {
        prev_len = total < RING_SIZE ? (size_t)total : RING_SIZE;
        for (size_t i = 0; i < prev_len; i++) {
            prev[i] = ring[(total - prev_len + i) % RING_SIZE];
        }
    }
    total = 0;
    magic = MAGIC;
    serial = esp_log_set_vprintf(capture);
    events = heap_caps_calloc(DB_EVENTS, sizeof(*events), MALLOC_CAP_SPIRAM);
}

size_t db_log_prev(char *buf, size_t cap)
{
    size_t n = prev_len < cap ? prev_len : cap;
    memcpy(buf, prev + prev_len - n, n);
    return n;
}

size_t db_log_since(uint64_t *pos, char *buf, size_t cap)
{
    portENTER_CRITICAL(&mux);
    uint64_t oldest = total > RING_SIZE ? total - RING_SIZE : 0;
    uint64_t from = *pos < oldest ? oldest : *pos;
    uint64_t lost = from - *pos;
    portEXIT_CRITICAL(&mux);
    size_t mark = lost ? (size_t)snprintf(buf, cap, "[... %llu bytes lost]\n", (unsigned long long)lost) : 0;
    mark = mark < cap ? mark : 0;
    portENTER_CRITICAL(&mux);
    oldest = total > RING_SIZE ? total - RING_SIZE : 0; /* the ring may have moved meanwhile */
    from = from < oldest ? oldest : from;
    size_t len = total - from < cap - mark ? (size_t)(total - from) : cap - mark;
    for (size_t i = 0; i < len; i++) {
        buf[mark + i] = ring[(from + i) % RING_SIZE];
    }
    *pos = from + len;
    portEXIT_CRITICAL(&mux);
    return mark + len;
}

size_t db_log_read(char *buf, size_t cap)
{
    portENTER_CRITICAL(&mux);
    uint64_t pos = total > cap ? total - cap : 0;
    portEXIT_CRITICAL(&mux);
    return db_log_since(&pos, buf, cap);
}

static int64_t marks[DB_MARK_COUNT];

void db_log_mark(db_mark_t mark)
{
    if (!marks[mark]) {
        marks[mark] = esp_timer_get_time() / 1000;
    }
}

int64_t db_log_mark_ms(db_mark_t mark)
{
    return marks[mark];
}

const char *db_log_mark_name(db_mark_t mark)
{
    static const char *const names[DB_MARK_COUNT] = {
        "deck", "lan_link", "lan_ip", "wlan_ip", "companion", "app", "server", "host_draws", "satellite"};
    return names[mark];
}

void db_log_event(const char *tag, const char *kind, const char *fmt, ...)
{
    db_event_t e = {.at_us = esp_timer_get_time()};
    time_t now = time(NULL);
    e.unix_s = now > 1700000000 ? now : 0; /* before SNTP the clock starts in 1970 */
    strlcpy(e.kind, kind, sizeof(e.kind));
    va_list args;
    va_start(args, fmt);
    vsnprintf(e.text, sizeof(e.text), fmt, args);
    va_end(args);
    ESP_LOGI(tag, "%s", e.text);
    if (!events) {
        return;
    }
    portENTER_CRITICAL(&mux);
    events[n_events++ % DB_EVENTS] = e;
    portEXIT_CRITICAL(&mux);
}

size_t db_log_events(db_event_t *out, size_t max)
{
    portENTER_CRITICAL(&mux);
    size_t n = n_events < DB_EVENTS ? n_events : DB_EVENTS;
    n = n < max ? n : max;
    for (size_t i = 0; i < n && events; i++) {
        out[i] = events[(n_events - n + i) % DB_EVENTS];
    }
    portEXIT_CRITICAL(&mux);
    return events ? n : 0;
}
