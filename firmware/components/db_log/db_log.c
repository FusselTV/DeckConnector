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
#include "freertos/task.h"
#include "nvs.h"

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
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static db_event_t *events; /* PSRAM ring, made in db_log_start() */
static uint32_t n_events;
static uint32_t n_kept;  /* events of this boot already in flash */
static uint32_t n_older; /* events loaded from earlier boots, at the ring's start */
static TaskHandle_t keeper;

/* In flash: "n" counts every event ever kept, event i sits in key "e<i % DB_EVENTS_KEPT>" as a kept_t, "boot"
 * counts the boots. */
typedef struct {
    int64_t unix_s;
    uint32_t boot;
    char kind[12];
    char text[84];
} kept_t;
static uint32_t boot_no;

static void key_of(uint32_t i, char *key)
{
    snprintf(key, 8, "e%u", (unsigned)(i % DB_EVENTS_KEPT));
}

/* The older boots' events, oldest first, before the ones this boot logged until now. */
static void load_kept(void)
{
    nvs_handle_t h;
    if (nvs_open("evlog", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint32_t n = 0;
    nvs_get_u32(h, "boot", &boot_no);
    nvs_set_u32(h, "boot", ++boot_no);
    nvs_get_u32(h, "n", &n);
    nvs_commit(h);
    portENTER_CRITICAL(&mux);
    uint32_t now_n = n_events < DB_EVENTS - DB_EVENTS_KEPT ? n_events : DB_EVENTS - DB_EVENTS_KEPT;
    portEXIT_CRITICAL(&mux);
    db_event_t *mine = heap_caps_malloc(now_n * sizeof(*mine) + 1, MALLOC_CAP_SPIRAM);
    if (!mine) {
        nvs_close(h);
        return;
    }
    portENTER_CRITICAL(&mux);
    memcpy(mine, events, now_n * sizeof(*mine));
    n_events = 0;
    portEXIT_CRITICAL(&mux);
    for (uint32_t i = n > DB_EVENTS_KEPT ? n - DB_EVENTS_KEPT : 0; i < n; i++) {
        kept_t k;
        size_t len = sizeof(k);
        char key[8];
        key_of(i, key);
        if (nvs_get_blob(h, key, &k, &len) != ESP_OK || len != sizeof(k) || boot_no - k.boot > 255) {
            continue;
        }
        db_event_t *e = &events[n_events++ % DB_EVENTS];
        *e = (db_event_t){.unix_s = k.unix_s, .boot = (uint8_t)(boot_no - k.boot)};
        memcpy(e->kind, k.kind, sizeof(e->kind));
        memcpy(e->text, k.text, sizeof(e->text));
        e->kind[sizeof(e->kind) - 1] = e->text[sizeof(e->text) - 1] = '\0';
    }
    nvs_close(h);
    portENTER_CRITICAL(&mux);
    n_older = n_events;
    for (uint32_t i = 0; i < now_n; i++) {
        events[n_events++ % DB_EVENTS] = mine[i];
    }
    portEXIT_CRITICAL(&mux);
    heap_caps_free(mine);
}

/* Writes this boot's new events to flash, at most every 2 s: a flash write stalls both CPUs for a moment, and
 * must not do so inside db_log_event(), which USB and network tasks call. */
static void keep_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));
        nvs_handle_t h;
        if (nvs_open("evlog", NVS_READWRITE, &h) != ESP_OK) {
            continue;
        }
        uint32_t n = 0;
        nvs_get_u32(h, "n", &n);
        for (;;) {
            portENTER_CRITICAL(&mux);
            if (n_events - (n_older + n_kept) > DB_EVENTS) { /* the ring moved on: skip what it overwrote */
                n_kept = n_events - n_older - DB_EVENTS;
            }
            bool more = n_older + n_kept < n_events;
            db_event_t e = more ? events[(n_older + n_kept) % DB_EVENTS] : (db_event_t){0};
            portEXIT_CRITICAL(&mux);
            if (!more) {
                break;
            }
            kept_t k = {.unix_s = e.unix_s, .boot = boot_no};
            memcpy(k.kind, e.kind, sizeof(k.kind));
            memcpy(k.text, e.text, sizeof(k.text));
            char key[8];
            key_of(n, key);
            if (nvs_set_blob(h, key, &k, sizeof(k)) != ESP_OK) {
                break;
            }
            n++;
            n_kept++;
        }
        nvs_set_u32(h, "n", n);
        nvs_commit(h);
        nvs_close(h);
    }
}

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

void db_log_keep_start(void)
{
    if (events) {
        load_kept();
        xTaskCreatePinnedToCore(keep_task, "evlog", 3072, NULL, 1, &keeper, 1);
        xTaskNotifyGive(keeper); /* this boot's first events, logged before flash was ready */
    }
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
    if (keeper) {
        xTaskNotifyGive(keeper);
    }
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
