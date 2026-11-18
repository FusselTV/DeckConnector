#include "db_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_timer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
static void (*sink)(const char *line, size_t len);
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static db_event_t *events; /* PSRAM ring, made in db_log_start() */
static uint32_t n_events;
static uint32_t n_kept;  /* events of this boot already in flash or skipped */
static uint32_t n_older; /* events loaded from earlier boots, at the ring's start */
static TaskHandle_t keeper;
static SemaphoreHandle_t keep_lock; /* the task and the shutdown handler both write */

/* In flash: event number `seq` sits in key "e<seq % DB_EVENTS_KEPT>"; "boot" counts the boots. */
typedef struct {
    int64_t unix_s;
    uint32_t since_s; /* after its boot's start: the time of the rows before the clock was set */
    uint32_t seq;
    uint32_t boot;
    char kind[12];
    char text[84];
} kept_t;
static uint32_t boot_no;
static uint32_t next_seq;

#define KEEP_EVERY_MS 30000 /* a flapping host must not wear out the flash that also holds the settings */
#define KEEP_PER_HOUR 120

static void key_of(uint32_t seq, char *key)
{
    snprintf(key, 8, "e%u", (unsigned)(seq % DB_EVENTS_KEPT));
}

/* The earlier boots' events, by their number, in front of the ones this boot logged before flash was up. */
static void load_kept(void)
{
    nvs_handle_t h;
    kept_t *k = heap_caps_malloc(DB_EVENTS_KEPT * sizeof(*k), MALLOC_CAP_SPIRAM);
    if (!k || nvs_open("evlog", NVS_READWRITE, &h) != ESP_OK) {
        heap_caps_free(k);
        return;
    }
    nvs_get_u32(h, "boot", &boot_no);
    nvs_set_u32(h, "boot", ++boot_no);
    nvs_commit(h);
    size_t n = 0;
    for (uint32_t i = 0; i < DB_EVENTS_KEPT; i++) {
        size_t len = sizeof(*k);
        char key[8];
        key_of(i, key);
        if (nvs_get_blob(h, key, &k[n], &len) == ESP_OK && len == sizeof(*k) && boot_no - k[n].boot <= 255) {
            next_seq = k[n].seq + 1 > next_seq ? k[n].seq + 1 : next_seq;
            n++;
        }
    }
    nvs_close(h);
    for (size_t i = 1; i < n; i++) { /* oldest first; a crash between writes leaves them out of key order */
        for (size_t m = i; m > 0 && k[m - 1].seq > k[m].seq; m--) {
            kept_t x = k[m];
            k[m] = k[m - 1];
            k[m - 1] = x;
        }
    }
    portENTER_CRITICAL(&mux);
    /* this boot's first events (fewer than DB_EVENTS - DB_EVENTS_KEPT) move behind the loaded ones */
    uint32_t mine = n_events < DB_EVENTS - n ? n_events : (uint32_t)(DB_EVENTS - n);
    for (uint32_t i = mine; i-- > 0;) {
        events[(i + n) % DB_EVENTS] = events[i % DB_EVENTS];
    }
    for (size_t i = 0; i < n; i++) {
        db_event_t *e = &events[i];
        *e = (db_event_t){.unix_s = k[i].unix_s,
                          .at_us = (int64_t)k[i].since_s * 1000000,
                          .boot = (uint8_t)(boot_no - k[i].boot)};
        memcpy(e->kind, k[i].kind, sizeof(e->kind));
        memcpy(e->text, k[i].text, sizeof(e->text));
        e->kind[sizeof(e->kind) - 1] = e->text[sizeof(e->text) - 1] = '\0';
    }
    n_older = n;
    n_events = n + mine;
    portEXIT_CRITICAL(&mux);
    heap_caps_free(k);
}

/* This boot's events not yet in flash, the last DB_EVENTS_KEPT at most: the others would be overwritten. */
static void keep_now(void)
{
    static int64_t hour_at;
    static int this_hour;
    xSemaphoreTake(keep_lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (now - hour_at > 3600LL * 1000000) {
        hour_at = now;
        this_hour = 0;
    }
    nvs_handle_t h;
    bool open = nvs_open("evlog", NVS_READWRITE, &h) == ESP_OK;
    static bool warned;
    while (open && this_hour < KEEP_PER_HOUR) {
        portENTER_CRITICAL(&mux);
        uint32_t newest_kept = n_events > DB_EVENTS_KEPT ? n_events - DB_EVENTS_KEPT : 0;
        if (n_older + n_kept < newest_kept) {
            n_kept = newest_kept - n_older;
        }
        bool more = n_older + n_kept < n_events;
        db_event_t e = more ? events[(n_older + n_kept) % DB_EVENTS] : (db_event_t){0};
        portEXIT_CRITICAL(&mux);
        if (!more) {
            break;
        }
        kept_t k = {
            .unix_s = e.unix_s, .since_s = (uint32_t)(e.at_us / 1000000), .seq = next_seq, .boot = boot_no};
        memcpy(k.kind, e.kind, sizeof(k.kind));
        memcpy(k.text, e.text, sizeof(k.text));
        char key[8];
        key_of(next_seq, key);
        if (nvs_set_blob(h, key, &k, sizeof(k)) != ESP_OK) {
            if (!warned) { /* not db_log_event: it would wake this task again */
                ESP_LOGW("evlog", "settings storage full, events stay in RAM");
            }
            warned = true;
            break;
        }
        warned = false;
        next_seq++;
        n_kept++;
        this_hour++;
    }
    if (open) {
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(keep_lock);
}

/* A flash write stalls both CPUs for a moment: never inside db_log_event(), which USB and network tasks call.
 */
static void keep_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        keep_now();
        vTaskDelay(pdMS_TO_TICKS(KEEP_EVERY_MS));
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
        if (sink && (size_t)n < sizeof(line)) {
            sink(line, len);
            return n;
        }
    }
    return serial(fmt, args);
}

void db_log_set_sink(void (*write)(const char *line, size_t len))
{
    sink = write;
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
    if (events && (keep_lock = xSemaphoreCreateMutex())) {
        load_kept();
        xTaskCreatePinnedToCore(keep_task, "evlog", 3072, NULL, 1, &keeper, 1);
        xTaskNotifyGive(keeper);                 /* this boot's first events, logged before flash was ready */
        esp_register_shutdown_handler(keep_now); /* a restart's last words, "update installed" and the like */
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
