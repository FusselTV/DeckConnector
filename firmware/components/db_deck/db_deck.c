#include "db_deck.h"

#include <string.h>

#include "db_driver.h"
#include "db_regions.h"
#include "db_screen.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb_port.h"

#define MAX_LISTENERS  4
#define PENDING_BUDGET (4 * 1024 * 1024)
#define INPUT_REPORT   64
#define EVENTS_PER_RPT 8

static const char *TAG = "deck";

typedef enum { EV_ATTACHED, EV_DETACHED, EV_INPUT, EV_SIMULATE } ev_kind_t;

typedef struct {
    ev_kind_t kind;
    uint8_t address;
    uint8_t len;
    uint64_t t_us;
    uint8_t report[INPUT_REPORT];
} ev_t;

static struct {
    SemaphoreHandle_t lock;    /* device, queue, writer */
    SemaphoreHandle_t io_lock; /* USB access */
    QueueHandle_t events;
    TaskHandle_t writer_task;
    db_dev_t dev;
    bool present;
    db_regions_t pending;
    db_source_t writer;
    db_deck_listener_t listeners[MAX_LISTENERS];
    int listener_count;
    struct {
        db_deck_ack_cb_t cb;
        void *arg;
    } ack[DB_SOURCE_COUNT];
    db_deck_stats_t stats;
    SemaphoreHandle_t shot_lock; /* screens; held while a whole screen is drawn again */
    db_screen_t screens[DB_SOURCE_COUNT];
    struct {
        uint8_t buf[32];
        size_t len;
    } feature[DB_SOURCE_COUNT]; /* last feature report (brightness) of a source that was not the writer */
    bool replay;
} s;

static void *psram_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

/* Draw again everything the new writer had on the deck, before anything new it sends. The UI is left out: it
 * draws a fresh screen whenever it takes over. */
static void replay(void)
{
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    db_source_t w = s.writer;
    bool present = s.present;
    s.replay = false;
    xSemaphoreGive(s.lock);
    if (present && s.feature[w].len) {
        xSemaphoreTake(s.io_lock, portMAX_DELAY);
        usb_port_io()->set_feature(s.feature[w].buf, s.feature[w].len);
        xSemaphoreGive(s.io_lock);
        s.feature[w].len = 0;
    }
    const db_screen_t *sc = &s.screens[w];
    for (int i = 0; present && w != DB_SOURCE_UI && i < sc->count; i++) {
        const db_shot_t *shot = &sc->shots[i];
        const db_target_t *t = db_device_find_target(&s.dev.info, shot->display, shot->rect);
        if (t) {
            xSemaphoreTake(s.io_lock, portMAX_DELAY);
            s.dev.driver->write_image(&s.dev, t, shot->rect, shot->data, shot->len);
            xSemaphoreGive(s.io_lock);
        }
    }
    xSemaphoreGive(s.shot_lock);
}

static void ack(const db_region_t *r, db_ack_result_t result, uint64_t t_start, uint64_t t_done)
{
    if (result == DB_ACK_SUPERSEDED) {
        s.stats.superseded++;
    } else if (result != DB_ACK_SHOWN) {
        s.stats.rejected++;
    }
    if (r->source < DB_SOURCE_COUNT && s.ack[r->source].cb) {
        s.ack[r->source].cb(result, r->display, r->seq, r->t_recv, t_start, t_done, s.ack[r->source].arg);
    }
}

/* `arg` carries the result to report, as a pointer to a db_ack_result_t. */
static void free_region(db_region_t *r, void *arg)
{
    ack(r, *(const db_ack_result_t *)arg, 0, 0);
    heap_caps_free(r->data);
    heap_caps_free(r);
}

static const db_ack_result_t superseded = DB_ACK_SUPERSEDED;
static const db_ack_result_t no_device = DB_ACK_NO_DEVICE;
static const db_ack_result_t wrong_role = DB_ACK_REJECTED_ROLE;

void db_deck_submit(db_source_t source, uint8_t display, db_rect_t rect, uint32_t seq, uint8_t *data,
                    size_t len, uint64_t t_recv)
{
    db_region_t tmp = {
        .source = (uint8_t)source, .display = display, .rect = rect, .seq = seq, .t_recv = t_recv};
    db_region_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_INTERNAL);
    db_ack_result_t reject = DB_ACK_SHOWN;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (!r) {
        reject = DB_ACK_BUSY;
    } else if (!s.present) {
        reject = DB_ACK_NO_DEVICE;
    } else if (source != s.writer) {
        reject = DB_ACK_REJECTED_ROLE;
    } else if (!db_device_find_target(&s.dev.info, display, rect)) {
        reject = DB_ACK_REJECTED_RECT;
    } else {
        *r = tmp;
        r->data = data;
        r->len = len;
        if (db_regions_put(&s.pending, r, free_region, (void *)&superseded) != DB_OK) {
            reject = DB_ACK_BUSY;
        }
    }
    xSemaphoreGive(s.lock);
    if (reject != DB_ACK_SHOWN) {
        ack(&tmp, reject, 0, 0);
        heap_caps_free(data);
        heap_caps_free(r);
        return;
    }
    xTaskNotifyGive(s.writer_task);
}

static void writer_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            if (s.replay) {
                replay();
            }
            xSemaphoreTake(s.lock, portMAX_DELAY);
            db_region_t *r = s.present ? db_regions_take(&s.pending) : NULL;
            const db_target_t *t = r ? db_device_find_target(&s.dev.info, r->display, r->rect) : NULL;
            xSemaphoreGive(s.lock);
            if (!r) {
                break;
            }
            xSemaphoreTake(s.io_lock, portMAX_DELAY);
            uint64_t t0 = (uint64_t)esp_timer_get_time();
            db_err_t err = t ? s.dev.driver->write_image(&s.dev, t, r->rect, r->data, r->len) : DB_ERR_STATE;
            uint64_t t1 = (uint64_t)esp_timer_get_time();
            xSemaphoreGive(s.io_lock);
            s.stats.images++;
            ack(r, err == DB_OK ? DB_ACK_SHOWN : DB_ACK_NO_DEVICE, t0, t1);
            if (err == DB_OK) {
                xSemaphoreTake(s.shot_lock, portMAX_DELAY);
                db_screen_put(&s.screens[r->source], r->display, r->rect, r->data, r->len);
                xSemaphoreGive(s.shot_lock);
            } else {
                heap_caps_free(r->data);
            }
            heap_caps_free(r);
        }
    }
}

static void notify_device(const db_device_info_t *info)
{
    for (int i = 0; i < s.listener_count; i++) {
        if (s.listeners[i].device_changed) {
            s.listeners[i].device_changed(info);
        }
    }
}

static void handle_attach(uint8_t address)
{
    uint16_t vid;
    uint16_t pid;
    if (usb_port_open(address, &vid, &pid) != ESP_OK) {
        return;
    }
    const db_driver_t *driver = db_driver_find(vid, pid);
    if (!driver) {
        ESP_LOGW(TAG, "no driver for %04x:%04x", vid, pid);
        usb_port_close();
        return;
    }
    xSemaphoreTake(s.io_lock, portMAX_DELAY);
    s.dev = (db_dev_t){.driver = driver, .io = usb_port_io()};
    db_err_t err = driver->open(&s.dev, vid, pid);
    xSemaphoreGive(s.io_lock);
    if (err != DB_OK) {
        ESP_LOGW(TAG, "driver %s failed to open the device: %d", driver->name, err);
        usb_port_close();
        return;
    }
    ESP_LOGI(TAG, "%s %s, serial %s, firmware %s", s.dev.info.vendor, s.dev.info.model, s.dev.info.serial,
             s.dev.info.firmware);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.present = true;
    xSemaphoreGive(s.lock);
    notify_device(&s.dev.info);
}

static void handle_detach(void)
{
    ESP_LOGW(TAG, "device unplugged");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.present = false;
    db_regions_clear(&s.pending, free_region, (void *)&no_device);
    xSemaphoreGive(s.lock);
    xSemaphoreTake(s.io_lock, portMAX_DELAY);
    usb_port_close();
    xSemaphoreGive(s.io_lock);
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    for (int i = 0; i < DB_SOURCE_COUNT; i++) {
        db_screen_clear(&s.screens[i]);
    }
    xSemaphoreGive(s.shot_lock);
    notify_device(NULL);
}

static void handle_input(const ev_t *ev)
{
    s.stats.inputs++;
    for (int i = 0; i < s.listener_count; i++) {
        if (s.listeners[i].raw_input) {
            s.listeners[i].raw_input(ev->report, ev->len);
        }
    }
    db_input_t out[EVENTS_PER_RPT];
    size_t n = s.dev.driver->parse_input(&s.dev, ev->report, ev->len, ev->t_us, out, EVENTS_PER_RPT);
    for (size_t e = 0; e < n; e++) {
        for (int i = 0; i < s.listener_count; i++) {
            if (s.listeners[i].input) {
                s.listeners[i].input(&out[e]);
            }
        }
    }
}

static void event_task(void *arg)
{
    ev_t ev;
    for (;;) {
        xQueueReceive(s.events, &ev, portMAX_DELAY);
        switch (ev.kind) {
        case EV_ATTACHED:
            handle_attach(ev.address);
            break;
        case EV_DETACHED:
            handle_detach();
            break;
        case EV_INPUT:
            if (s.present) {
                handle_input(&ev);
            }
            break;
        case EV_SIMULATE: {
            /* built here, so it starts from the key states every earlier report left */
            db_input_t in;
            memcpy(&in, ev.report, sizeof(in));
            size_t len = s.present ? s.dev.driver->build_input(&s.dev, &in, ev.report, sizeof(ev.report)) : 0;
            if (len) {
                ev.len = (uint8_t)len;
                handle_input(&ev);
            }
            break;
        }
        }
    }
}

static void on_attached(uint8_t address)
{
    ev_t ev = {.kind = EV_ATTACHED, .address = address};
    xQueueSend(s.events, &ev,
               pdMS_TO_TICKS(100)); /* wait a little: a lost plug event leaves `present` wrong */
}

static void on_detached(void)
{
    ev_t ev = {.kind = EV_DETACHED};
    xQueueSend(s.events, &ev, pdMS_TO_TICKS(100));
}

static void on_input(const uint8_t *report, size_t len, uint64_t t_us)
{
    ev_t ev = {.kind = EV_INPUT, .t_us = t_us, .len = (uint8_t)(len < INPUT_REPORT ? len : INPUT_REPORT)};
    memcpy(ev.report, report, ev.len);
    if (xQueueSend(s.events, &ev, 0) != pdTRUE) {
        s.stats.inputs_dropped++;
    }
}

esp_err_t db_deck_simulate(const db_input_t *in)
{
    _Static_assert(sizeof(db_input_t) <= INPUT_REPORT, "input does not fit the event");
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    ev_t ev = {.kind = EV_SIMULATE, .t_us = (uint64_t)esp_timer_get_time()};
    memcpy(ev.report, in, sizeof(*in));
    return xQueueSend(s.events, &ev, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t db_deck_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.io_lock = xSemaphoreCreateMutex();
    s.events = xQueueCreate(64, sizeof(ev_t));
    s.shot_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < DB_SOURCE_COUNT; i++) {
        db_screen_init(&s.screens[i], psram_realloc, heap_caps_free);
    }
    db_regions_init(&s.pending, PENDING_BUDGET);
    xTaskCreatePinnedToCore(writer_task, "deck_writer", 4096, NULL, 8, &s.writer_task, 1);
    xTaskCreatePinnedToCore(event_task, "deck_events", 4096, NULL, 7, NULL, 1);
    usb_port_cb_t cb = {.attached = on_attached, .detached = on_detached, .input = on_input};
    return usb_port_start(&cb);
}

void db_deck_add_listener(const db_deck_listener_t *listener)
{
    if (s.listener_count < MAX_LISTENERS) {
        s.listeners[s.listener_count++] = *listener;
    }
}

void db_deck_set_ack_handler(db_source_t source, db_deck_ack_cb_t cb, void *arg)
{
    s.ack[source].cb = cb;
    s.ack[source].arg = arg;
}

void db_deck_set_writer(db_source_t source)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.writer != source) {
        ESP_LOGI(TAG, "writer %d -> %d", s.writer, source);
        s.writer = source;
        s.replay = true;
        db_regions_clear(&s.pending, free_region, (void *)&wrong_role);
    }
    xSemaphoreGive(s.lock);
    xTaskNotifyGive(s.writer_task);
}

const char *db_deck_source_name(db_source_t source)
{
    static const char *const names[DB_SOURCE_COUNT] = {"ui", "server1", "server2", "app", "companion"};
    return source < DB_SOURCE_COUNT ? names[source] : "";
}

db_source_t db_deck_writer(void)
{
    return s.writer;
}

bool db_deck_info(db_device_info_t *out)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool present = s.present;
    if (present) {
        *out = s.dev.info;
    }
    xSemaphoreGive(s.lock);
    return present;
}

static esp_err_t to_esp(db_err_t err)
{
    return err == DB_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t db_deck_set_brightness(uint8_t pct)
{
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.io_lock, portMAX_DELAY);
    db_err_t err = s.dev.driver->set_brightness(&s.dev, pct);
    xSemaphoreGive(s.io_lock);
    return to_esp(err);
}

esp_err_t db_deck_raw_write(db_source_t source, const uint8_t *report, size_t len)
{
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    const db_hid_io_t *io = usb_port_io();
    uint8_t padded[1024] = {0}; /* hosts may send short reports; the endpoint wants full ones */
    memcpy(padded, report, len < io->out_report_size ? len : io->out_report_size);
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    db_chunk_t chunk;
    if (s.dev.driver->parse_output(&s.dev, padded, io->out_report_size, &chunk)) {
        if (db_screen_feed(&s.screens[source], &chunk) != DB_OK) {
            ESP_LOGW(TAG, "%s: image at %u,%u dropped from the kept picture", db_deck_source_name(source),
                     chunk.rect.x, chunk.rect.y);
        }
    }
    db_err_t err = DB_OK;
    if (s.writer == source) {
        xSemaphoreTake(s.io_lock, portMAX_DELAY);
        err = io->write(padded, io->out_report_size);
        xSemaphoreGive(s.io_lock);
    }
    xSemaphoreGive(s.shot_lock);
    return to_esp(err);
}

esp_err_t db_deck_raw_get_feature(uint8_t *buf, size_t len, size_t *out_len)
{
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.io_lock, portMAX_DELAY);
    db_err_t err = usb_port_io()->get_feature(buf, len, out_len);
    xSemaphoreGive(s.io_lock);
    return to_esp(err);
}

esp_err_t db_deck_raw_set_feature(db_source_t source, const uint8_t *buf, size_t len)
{
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    db_err_t err = DB_OK;
    if (s.writer == source) {
        xSemaphoreTake(s.io_lock, portMAX_DELAY);
        err = usb_port_io()->set_feature(buf, len);
        xSemaphoreGive(s.io_lock);
    } else if (len <= sizeof(s.feature[source].buf)) {
        memcpy(s.feature[source].buf, buf, len);
        s.feature[source].len = len;
    }
    xSemaphoreGive(s.shot_lock);
    return to_esp(err);
}

size_t db_deck_shots(db_source_t *writer, db_shot_t *out, size_t max)
{
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    *writer = s.writer;
    const db_screen_t *sc = &s.screens[s.writer];
    size_t n = 0;
    for (int i = 0; i < sc->count && n < max; i++, n++) {
        out[n] = sc->shots[i];
        out[n].data = NULL;
    }
    xSemaphoreGive(s.shot_lock);
    return n;
}

esp_err_t db_deck_shot_copy(uint32_t id, uint8_t **data, size_t *len)
{
    esp_err_t err = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s.shot_lock, portMAX_DELAY);
    for (int src = 0; src < DB_SOURCE_COUNT && err == ESP_ERR_NOT_FOUND; src++) {
        const db_screen_t *sc = &s.screens[src];
        for (int i = 0; i < sc->count; i++) {
            if (sc->shots[i].id == id) {
                *data = heap_caps_malloc(sc->shots[i].len, MALLOC_CAP_SPIRAM);
                err = *data ? ESP_OK : ESP_ERR_NO_MEM;
                if (*data) {
                    memcpy(*data, sc->shots[i].data, sc->shots[i].len);
                    *len = sc->shots[i].len;
                }
                break;
            }
        }
    }
    xSemaphoreGive(s.shot_lock);
    return err;
}

void db_deck_stats(db_deck_stats_t *out)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    *out = s.stats;
    out->usb_errors = usb_port_errors();
    xSemaphoreGive(s.lock);
}
