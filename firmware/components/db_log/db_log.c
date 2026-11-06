#include "db_log.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define RING_SIZE (16 * 1024)

static char *ring;
static uint64_t total; /* bytes ever written: byte p sits at ring[p % RING_SIZE] while it is in the window */
static vprintf_like_t serial;
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
    ring = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    if (ring) {
        serial = esp_log_set_vprintf(capture);
    }
}

size_t db_log_since(uint64_t *pos, char *buf, size_t cap)
{
    if (!ring) {
        return 0;
    }
    portENTER_CRITICAL(&mux);
    uint64_t oldest = total > RING_SIZE ? total - RING_SIZE : 0;
    uint64_t from = *pos < oldest ? oldest : *pos;
    size_t len = total - from < cap ? (size_t)(total - from) : cap;
    for (size_t i = 0; i < len; i++) {
        buf[i] = ring[(from + i) % RING_SIZE];
    }
    *pos = from + len;
    portEXIT_CRITICAL(&mux);
    return len;
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
    static const char *const names[DB_MARK_COUNT] = {"deck",      "lan_link", "lan_ip", "wlan_ip",
                                                     "companion", "app",      "server", "host_draws",
                                                     "satellite"};
    return names[mark];
}
