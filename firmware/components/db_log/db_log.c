#include "db_log.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define RING_SIZE (16 * 1024)

static char *ring;
static size_t head; /* next write position */
static bool wrapped;
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
            ring[head] = line[i];
            head = (head + 1) % RING_SIZE;
            wrapped |= head == 0;
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

size_t db_log_read(char *buf, size_t cap)
{
    if (!ring || cap == 0) {
        return 0;
    }
    portENTER_CRITICAL(&mux);
    size_t len = wrapped ? RING_SIZE : head;
    size_t start = wrapped ? head : 0;
    if (len > cap) {
        start = (start + len - cap) % RING_SIZE;
        len = cap;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = ring[(start + i) % RING_SIZE];
    }
    portEXIT_CRITICAL(&mux);
    return len;
}
