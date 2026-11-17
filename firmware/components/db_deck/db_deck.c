#include "db_deck.h"
#include "db_log.h"
#include "db_ota.h"

#include <string.h>

#include "db_driver.h"
#include "db_regions.h"
#include "db_screen.h"
#include "db_wake.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_port.h"
#include "nvs.h"
#include "usb_port.h"

#define MAX_LISTENERS  6
#define PENDING_BUDGET (4 * 1024 * 1024) /* shared by max_decks */
#define INPUT_REPORT   65                /* the six-key report */
#define EVENTS_PER_RPT 8
#define CYCLE_FLOOR_US 10000000LL /* between two root port cycles of the recovery: they take every deck */
/* kept for plugs: a cycle takes every deck, then they and the hub come again */
#define PLUG_ROOM (2 * USB_PORT_DEVS + 1)

static const char *TAG = "deck";

typedef enum { EV_ATTACHED, EV_DETACHED, EV_INPUT, EV_SIMULATE, EV_UNPARK } ev_kind_t;

typedef struct {
    ev_kind_t kind;
    uint8_t address;
    int8_t port; /* EV_DETACHED, EV_INPUT: the usb_port device */
    int8_t deck; /* EV_SIMULATE */
    uint8_t len;
    bool real;
    uint32_t only_ip;
    uint64_t t_us;
    uint8_t report[INPUT_REPORT];
} ev_t;

/* One slot. Its locks, task and pictures stay when the deck goes: the same deck back shows them at once. A
 * hung deck holds only its own io lock and writer task. */
typedef struct {
    SemaphoreHandle_t lock;    /* device, queue, writer */
    SemaphoreHandle_t io_lock; /* USB access */
    TaskHandle_t writer_task;
    int port; /* the usb_port device while present */
    db_dev_t dev;
    /* simulated input has its own key states, or a test's held key would turn a person's next real report
     * into a release nobody made. Swapped in for it: both run in the event task. */
    uint8_t sim_state[sizeof(((db_dev_t *)0)->state)];
    bool present;
    bool host_wrote; /* a host wrote since the deck was plugged in */
    db_regions_t pending;
    db_source_t writer;
    SemaphoreHandle_t shot_lock; /* screens; held while a whole screen is drawn again */
    db_screen_t screens[DB_SOURCE_COUNT];
    struct {
        uint8_t buf[32];
        size_t len;
    } feature[DB_SOURCE_COUNT]; /* last feature report of a source that was not the writer */
    uint8_t
        bright[DB_SOURCE_COUNT]; /* a host's last brightness + 1, 0: none; it gets it back after the menu */
    bool replay;
    bool keep_picture; /* of the old writer, until the new one draws: under lock */
    bool clear_first;  /* the display goes black before the writer's first image */
    int brightness;    /* nonzero: the writer task sets `lit`, 0 while dark */
    int lit;           /* the brightness the deck has, -1: not known; atomic */
    char last_serial[sizeof(((db_device_info_t *)0)->serial)];
    db_shot_t replay_shots[DB_SCREEN_MAX];
    uint8_t *black; /* a black full display, made once */
    size_t black_len;
    db_stuck_state_t stuck; /* kept over the replug, which is what it is for */
} deck_t;

static deck_t decks[DB_MAX_DECKS] EXT_RAM_BSS_ATTR; /* zero until db_deck_start */

static struct {
    QueueHandle_t events;
    db_deck_listener_t listeners[MAX_LISTENERS];
    int listener_count;
    struct {
        db_deck_ack_cb_t cb;
        void *arg;
    } ack[DB_SOURCE_COUNT];
    db_deck_stats_t stats;
    int max_decks;
    int8_t port_deck[USB_PORT_DEVS];      /* the slot of each usb_port device, -1: none; event task only */
    uint8_t waiting[USB_PORT_DEVS];       /* decks over the limit, by USB address; event task only */
    int over;                             /* of them still plugged in, counted by the event task */
    db_slot_owner_t owners[DB_MAX_DECKS]; /* whose slot is whose, kept in NVS */
} s;

static portMUX_TYPE wake_mux = portMUX_INITIALIZER_UNLOCKED;
static db_wake_t wake;

static bool dark(void)
{
    return __atomic_load_n(&wake.dark, __ATOMIC_RELAXED);
}

static void *psram_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

/* One display behind all keys: a host that draws only its keys leaves the pixels around them as the last
 * writer painted them, so the whole display goes black first. */
static void clear_display(deck_t *k)
{
    const db_display_t *d = &k->dev.info.displays[0];
    const db_target_t *full = NULL;
    for (uint8_t i = 0; i < d->target_count && !full; i++) {
        full = d->targets[i].kind == DB_TARGET_FULL ? &d->targets[i] : NULL;
    }
    if (!full) {
        return;
    }
    if (!k->black) { /* another model in this slot gets a new one in handle_attach */
        uint8_t *rgb = jpeg_port_alloc_rgb(full->rect.w, full->rect.h);
        if (rgb) {
            memset(rgb, 0, (size_t)full->rect.w * full->rect.h * 3);
            jpeg_port_init();
            jpeg_port_encode_for(d, rgb, full->rect.w, full->rect.h, 80, &k->black, &k->black_len);
            heap_caps_free(rgb);
        }
    }
    if (k->black) {
        xSemaphoreTake(k->io_lock, portMAX_DELAY);
        k->dev.driver->write_image(&k->dev, full, full->rect, k->black, k->black_len);
        xSemaphoreGive(k->io_lock);
    }
}

/* The percent of a host's brightness report (gen 2: 03 08 pct, gen 1: 05 55 aa d1 01 pct, padded to the
 * report length), else -1. */
static int brightness_of(const uint8_t *buf, size_t len)
{
    static const uint8_t gen2[] = {0x03, 0x08}, gen1[] = {0x05, 0x55, 0xaa, 0xd1, 0x01};
    if (len > sizeof(gen2) && memcmp(buf, gen2, sizeof(gen2)) == 0) {
        return buf[sizeof(gen2)];
    }
    return len > sizeof(gen1) && memcmp(buf, gen1, sizeof(gen1)) == 0 ? buf[sizeof(gen1)] : -1;
}

/* Draw again everything the new writer had on the deck, before anything new it sends. The UI is left out: it
 * draws a fresh screen whenever it takes over. On a handover, a writer without a kept picture leaves the old
 * one until its first image, so it shows no black frame. Runs in the writer task, so images that arrive
 * meanwhile are drawn after it. Each image is copied out: the kept picture must not stay locked for USB
 * transfers, a fresh deck can take half a second per report. */
static void replay(int deck)
{
    deck_t *k = &decks[deck];
    db_shot_t *shots = k->replay_shots;
    xSemaphoreTake(k->lock, portMAX_DELAY);
    db_source_t w = k->writer;
    bool present = k->present;
    bool keep = k->keep_picture;
    k->replay = false;
    xSemaphoreGive(k->lock);
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    uint8_t feature[sizeof(k->feature[0].buf)];
    size_t feature_len = k->feature[w].len;
    memcpy(feature, k->feature[w].buf, feature_len);
    k->feature[w].len = 0;
    int bright = k->bright[w] - 1;
    int n = w == DB_SOURCE_UI ? 0 : k->screens[w].count;
    memcpy(shots, k->screens[w].shots, (size_t)n * sizeof(shots[0]));
    xSemaphoreGive(k->shot_lock);
    if (!present) {
        return;
    }
    k->clear_first = n == 0 && keep;
    if (!k->clear_first) {
        clear_display(k);
    }
    if (bright >= 0) {
        __atomic_store_n(&k->lit, bright, __ATOMIC_RELAXED); /* before dark(): a wake after it lights this */
        xSemaphoreTake(k->io_lock, portMAX_DELAY);
        k->dev.driver->set_brightness(&k->dev, dark() ? 0 : (uint8_t)bright);
        xSemaphoreGive(k->io_lock);
    }
    if (feature_len) {
        xSemaphoreTake(k->io_lock, portMAX_DELAY);
        k->dev.io->set_feature(k->dev.io, feature, feature_len);
        xSemaphoreGive(k->io_lock);
    }
    for (int i = 0; i < n; i++) {
        uint8_t *data = NULL;
        size_t len = 0;
        const db_target_t *t = db_device_find_target(&k->dev.info, shots[i].display, shots[i].rect);
        if (!t || db_deck_shot_copy(deck, shots[i].id, &data, &len) != ESP_OK) {
            continue; /* replaced meanwhile: the newer one is queued */
        }
        xSemaphoreTake(k->io_lock, portMAX_DELAY);
        k->dev.driver->write_image(&k->dev, t, shots[i].rect, data, len);
        xSemaphoreGive(k->io_lock);
        heap_caps_free(data);
    }
}

static void ack(const db_region_t *r, db_ack_result_t result, uint64_t t_start, uint64_t t_done)
{
    if (result == DB_ACK_SUPERSEDED) {
        __atomic_add_fetch(&s.stats.superseded, 1, __ATOMIC_RELAXED);
    } else if (result != DB_ACK_SHOWN) {
        __atomic_add_fetch(&s.stats.rejected, 1, __ATOMIC_RELAXED);
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

void db_deck_submit(int deck, db_source_t source, uint8_t display, db_rect_t rect, uint32_t seq,
                    uint8_t *data, size_t len, uint64_t t_recv)
{
    deck_t *k = &decks[deck];
    db_region_t tmp = {
        .source = (uint8_t)source, .display = display, .rect = rect, .seq = seq, .t_recv = t_recv};
    db_region_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_INTERNAL);
    db_ack_result_t reject = DB_ACK_SHOWN;
    xSemaphoreTake(k->lock, portMAX_DELAY);
    if (!r) {
        reject = DB_ACK_BUSY;
    } else if (!k->present) {
        reject = DB_ACK_NO_DEVICE;
    } else if (!db_device_find_target(&k->dev.info, display, rect)) {
        reject = DB_ACK_REJECTED_RECT;
    } else if (source == DB_SOURCE_SAT && source != k->writer) {
        /* under the lock: a writer change cannot slip in between and replay the picture without it */
        xSemaphoreTake(k->shot_lock, portMAX_DELAY);
        db_screen_put(&k->screens[source], display, rect, data, len);
        xSemaphoreGive(k->shot_lock);
        xSemaphoreGive(k->lock);
        heap_caps_free(r);
        return;
    } else if (source != k->writer) {
        reject = DB_ACK_REJECTED_ROLE;
    } else {
        *r = tmp;
        r->data = data;
        r->len = len;
        if (db_regions_put(&k->pending, r, free_region, (void *)&superseded) != DB_OK) {
            reject = DB_ACK_BUSY;
        }
    }
    xSemaphoreGive(k->lock);
    if (reject != DB_ACK_SHOWN) {
        ack(&tmp, reject, 0, 0);
        heap_caps_free(data);
        heap_caps_free(r);
        return;
    }
    xTaskNotifyGive(k->writer_task);
}

static void writer_task(void *arg)
{
    int deck = (int)(intptr_t)arg;
    deck_t *k = &decks[deck];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            /* between images, also in a stream; read now, so a wake or a dark that raced it still wins */
            int lit = __atomic_load_n(&k->lit, __ATOMIC_RELAXED);
            if (__atomic_exchange_n(&k->brightness, 0, __ATOMIC_RELAXED) && k->present &&
                (lit >= 0 || dark())) {
                xSemaphoreTake(k->io_lock, portMAX_DELAY);
                k->dev.driver->set_brightness(&k->dev, dark() ? 0 : (uint8_t)lit);
                xSemaphoreGive(k->io_lock);
            }
            if (k->replay) {
                replay(deck);
            }
            xSemaphoreTake(k->lock, portMAX_DELAY);
            db_region_t *r = k->present ? db_regions_take(&k->pending) : NULL;
            const db_target_t *t = r ? db_device_find_target(&k->dev.info, r->display, r->rect) : NULL;
            xSemaphoreGive(k->lock);
            if (!r) {
                break;
            }
            if (k->clear_first && t) {
                k->clear_first = false;
                clear_display(k);
            }
            xSemaphoreTake(k->io_lock, portMAX_DELAY);
            uint64_t t0 = (uint64_t)esp_timer_get_time();
            db_err_t err =
                t ? k->dev.driver->write_image(&k->dev, t, r->rect, r->data, r->len) : DB_ERR_STATE;
            uint64_t t1 = (uint64_t)esp_timer_get_time();
            xSemaphoreGive(k->io_lock);
            __atomic_add_fetch(&s.stats.images, 1, __ATOMIC_RELAXED);
            ack(r, err == DB_OK ? DB_ACK_SHOWN : DB_ACK_NO_DEVICE, t0, t1);
            bool dock = r->source >= DB_SOURCE_DOCK0 && r->source < DB_SOURCE_SAT;
            if (err == DB_OK && !dock) { /* a dock host's picture keeps it already */
                xSemaphoreTake(k->shot_lock, portMAX_DELAY);
                db_screen_put(&k->screens[r->source], r->display, r->rect, r->data, r->len);
                xSemaphoreGive(k->shot_lock);
            } else {
                heap_caps_free(r->data);
            }
            heap_caps_free(r);
        }
    }
}

static void notify_device(int deck, const db_device_info_t *info)
{
    for (int i = 0; i < s.listener_count; i++) {
        if (s.listeners[i].device_changed) {
            s.listeners[i].device_changed(deck, info);
        }
    }
}

static bool start_replug(uint8_t address, uint32_t off_ms);

#define STUCK_RESTARTED 0x57a11ed0
static RTC_NOINIT_ATTR uint32_t stuck_restarted; /* kept over a restart, not over a power cut */

/* Every root port cycle takes all decks: two of them are CYCLE_FLOOR_US apart, whatever asked for them.
 * Writer tasks, the USB client task and the event task ask. */
static portMUX_TYPE cycle_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t cycled;
static int64_t settled; /* until then the decks come back from a root port cycle */

static bool take_cycle(int64_t now)
{
    taskENTER_CRITICAL(&cycle_mux);
    bool due = !cycled || now - cycled >= CYCLE_FLOOR_US;
    cycled = due ? now : cycled;
    taskEXIT_CRITICAL(&cycle_mux);
    return due;
}

static int present_count(void);

static void on_stuck(int port)
{
    int deck = s.port_deck[port];
    if (deck < 0) {
        return;
    }
    deck_t *k = &decks[deck];
    bool others = present_count() > 1;
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&cycle_mux);
    bool restarted = stuck_restarted == STUCK_RESTARTED;
    db_stuck_t next =
        db_stuck_next(&k->stuck, now, !cycled || now - cycled >= CYCLE_FLOOR_US, others, &restarted);
    stuck_restarted = restarted ? STUCK_RESTARTED : 0;
    cycled = next == DB_STUCK_CYCLE ? now : cycled;
    int64_t backoff = k->stuck.backoff;
    taskEXIT_CRITICAL(&cycle_mux);
    if (next == DB_STUCK_RESTART) {
        ESP_LOGE(TAG, "deck %d hung again after its reconnect: restarting", deck + 1);
        db_ota_restart(0);
    } else if (next == DB_STUCK_CYCLE) {
        if (backoff) {
            ESP_LOGW(TAG, "deck %d hung again: the next reconnect for it in %lld s at the earliest", deck + 1,
                     backoff / 1000000);
        }
        start_replug(0, 500);
    }
}

static int present_count(void)
{
    int n = 0;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        n += decks[d].present;
    }
    return n;
}

/* False when it waits already. */
static bool park(uint8_t address)
{
    int empty = -1;
    for (int i = USB_PORT_DEVS - 1; i >= 0; i--) {
        if (s.waiting[i] == address) {
            return false;
        }
        empty = s.waiting[i] ? empty : i;
    }
    if (empty < 0) {
        return false;
    }
    s.waiting[empty] = address;
    return true;
}

static void unpark_one(uint8_t address)
{
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        s.waiting[i] = s.waiting[i] == address ? 0 : s.waiting[i];
    }
}

/* A parked deck that was unplugged no longer counts. */
static void count_parked(void)
{
    int n = 0;
    uint16_t vid;
    uint16_t pid;
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        if (s.waiting[i] && usb_port_ids(s.waiting[i], &vid, &pid) != ESP_OK) {
            s.waiting[i] = 0;
        }
        n += s.waiting[i] != 0;
    }
    __atomic_store_n(&s.over, n, __ATOMIC_RELAXED);
}

static void on_attached(uint8_t address);

/* The waiting decks come again when there is room: a free place or a raised limit. One that went meanwhile is
 * not found. Each that gets in leaves the list, the others stay on it. */
static void unpark(void)
{
    if (present_count() >= s.max_decks) {
        return; /* a config save or the detach of a deck that is not counted: nothing changed for them */
    }
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        if (s.waiting[i]) {
            on_attached(s.waiting[i]);
        }
    }
}

static void save_owners(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("decks", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "slots", s.owners, sizeof(s.owners));
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "deck slots not saved: %s", esp_err_to_name(err));
    }
}

static int take_slot(int port, const char *serial)
{
    db_slot_owner_t id;
    if (serial[0]) {
        snprintf(id, sizeof(id), "%s", serial);
    } else {
        snprintf(id, sizeof(id), "port %s", usb_port_path(port));
    }
    bool taken[DB_MAX_DECKS];
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        taken[d] = decks[d].present;
    }
    taskENTER_CRITICAL(&cycle_mux);
    bool settling = esp_timer_get_time() < settled;
    taskEXIT_CRITICAL(&cycle_mux);
    /* after a cycle the deck that enumerates first would take the slot of one that is still on its way */
    if (settling && db_slot_others(s.owners, taken, s.max_decks, id)) {
        return -1;
    }
    int slot = db_slot_take(s.owners, taken, DB_MAX_DECKS, s.max_decks, id);
    save_owners(); /* NVS writes nothing when the blob is the same */
    return slot;
}

static void handle_attach(uint8_t address)
{
    /* The Stream Deck + stops answering about 1 s after it gets a USB address that is a multiple of 16 (seen
     * at 16, 32 and 48, never in between). Plugged in again it gets the next one. A hub or another device
     * there is left alone. Only the root port can be cycled (a hub port tripped an assert in esp-usb), so the
     * other decks go too; left alone the deck would hang and on_stuck would cycle it later anyway. */
    uint16_t vid;
    uint16_t pid;
    if ((address & 0x0f) == 0 && present_count() < s.max_decks &&
        usb_port_ids(address, &vid, &pid) == ESP_OK && db_driver_find(vid, pid)) {
        if (take_cycle(esp_timer_get_time())) {
            ESP_LOGW(TAG, "USB address %u, plugging the decks in again", address);
            start_replug(address, 500);
            return;
        }
        ESP_LOGW(TAG, "USB address %u, but the port was cycled less than 10 s ago", address);
    }
    int port;
    esp_err_t opened = usb_port_open(address, &port, &vid, &pid);
    if (opened != ESP_OK) {
        if (opened == ESP_ERR_NO_MEM) {
            park(address);
        }
        return;
    }
    const db_driver_t *driver = db_driver_find(vid, pid);
    if (!driver) {
        ESP_LOGW(TAG, "no driver for %04x:%04x", vid, pid);
        usb_port_close(port);
        return;
    }
    static db_dev_t dev; /* too big for the event task's stack, and only it gets here */
    dev = (db_dev_t){.driver = driver, .io = usb_port_io(port)};
    db_err_t err = driver->open(&dev, vid, pid);
    if (err != DB_OK) {
        ESP_LOGW(TAG, "driver %s failed to open the device: %d", driver->name, err);
        usb_port_close(port);
        return;
    }
    bool full = present_count() >= s.max_decks;
    int deck = full ? -1 : take_slot(port, dev.info.serial);
    if (deck < 0) {
        driver->set_brightness(&dev, 0); /* lit, it would show its logo and draw current */
        usb_port_close(port);
        if (park(address) && full) {
            db_log_event(TAG, "deck", "deck at USB address %u left dark: max_decks is %d", address,
                         s.max_decks);
        }
        count_parked();
        return;
    }
    unpark_one(address);
    deck_t *k = &decks[deck];
    usb_port_product(port, dev.info.product, sizeof(dev.info.product));
    if (!k->writer_task && xTaskCreatePinnedToCore(writer_task, "deck_writer", 4096, (void *)(intptr_t)deck,
                                                   8, &k->writer_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no writer task for deck %d", deck + 1);
        usb_port_close(port);
        return;
    }
    xSemaphoreTake(k->io_lock, portMAX_DELAY);
    bool other_model = k->dev.info.pid != dev.info.pid;
    k->dev = dev;
    k->port = port;
    memcpy(k->sim_state, k->dev.state, sizeof(k->sim_state));
    if (other_model) {
        heap_caps_free(k->black);
        k->black = NULL;
    }
    xSemaphoreGive(k->io_lock);
    s.port_deck[port] = (int8_t)deck;
    ESP_LOGI(TAG, "deck %d: %s %s, serial %s, firmware %s", deck + 1, dev.info.vendor, dev.info.model,
             dev.info.serial, dev.info.firmware);
    db_log_mark(DB_MARK_DECK);
    /* another deck: the kept pictures have its predecessor's layout. The same one: draw them again at once,
     * it lost its picture with its power, and a host may not send it again. */
    bool same = dev.info.serial[0] && strcmp(k->last_serial, dev.info.serial) == 0;
    strlcpy(k->last_serial, dev.info.serial, sizeof(k->last_serial));
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    for (int i = 0; i < DB_SOURCE_COUNT && !same; i++) {
        db_screen_clear(&k->screens[i]);
    }
    xSemaphoreGive(k->shot_lock);
    xSemaphoreTake(k->lock, portMAX_DELAY);
    db_regions_init(&k->pending, PENDING_BUDGET / s.max_decks); /* empty while the deck was gone */
    k->present = true;
    k->host_wrote = false;
    k->replay = same;
    xSemaphoreGive(k->lock);
    notify_device(deck, &k->dev.info);
    if (same) {
        xTaskNotifyGive(k->writer_task);
    }
}

static void handle_detach(int port)
{
    int deck = s.port_deck[port];
    if (deck < 0) {
        return;
    }
    deck_t *k = &decks[deck];
    ESP_LOGW(TAG, "deck %d unplugged", deck + 1);
    xSemaphoreTake(k->lock, portMAX_DELAY);
    k->present = false;
    db_regions_clear(&k->pending, free_region, (void *)&no_device);
    taskENTER_CRITICAL(&wake_mux);
    db_wake_forget(&wake, deck); /* its releases never come */
    taskEXIT_CRITICAL(&wake_mux);
    xSemaphoreGive(k->lock);
    xSemaphoreTake(k->io_lock, portMAX_DELAY);
    usb_port_close(port);
    xSemaphoreGive(k->io_lock);
    s.port_deck[port] = -1;
    notify_device(deck, NULL); /* the kept pictures stay: the same deck plugged in again shows them at once */
    unpark();
}

/* Each writer sets 0 or what its host set last, as dark() is when it gets there. */
static void light_all(bool off)
{
    db_log_event(TAG, "deck", off ? "screen off" : "screen on");
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (decks[d].present) {
            __atomic_store_n(&decks[d].brightness, 1, __ATOMIC_RELAXED);
            xTaskNotifyGive(decks[d].writer_task);
        }
    }
}

void db_deck_screen_off(bool off)
{
    taskENTER_CRITICAL(&wake_mux);
    bool changed = db_wake_set(&wake, off, esp_timer_get_time());
    taskEXIT_CRITICAL(&wake_mux);
    if (changed) {
        light_all(off);
    }
}

void db_deck_screen_tick(int off_s, bool covered)
{
    int64_t now = esp_timer_get_time();
    bool off = false;
    taskENTER_CRITICAL(&wake_mux);
    /* a question or the menu restarts the idle time: they must not go dark the moment it closes */
    bool changed = covered ? db_wake_set(&wake, false, now)
                           : db_wake_due(&wake, off_s, false, now, &off) && db_wake_set(&wake, off, now);
    taskEXIT_CRITICAL(&wake_mux);
    if (changed) {
        light_all(off);
    }
}

bool db_deck_screen_is_off(void)
{
    return dark();
}

static bool input_real;
static bool input_from_deck;
static uint32_t input_only_ip;

bool db_deck_input_real(void)
{
    return input_real;
}

bool db_deck_input_from_deck(void)
{
    return input_from_deck;
}

bool db_deck_input_real_for(uint32_t peer_ip)
{
    return input_real && (!input_only_ip || input_only_ip == peer_ip);
}

/* `raw` is what dock hosts get, `ev->report` what the edges are parsed from: they differ for a simulated
 * press. */
static void handle_input(int deck, const ev_t *ev, const uint8_t *raw)
{
    deck_t *k = &decks[deck];
    s.stats.inputs++;
    input_real = ev->real;
    input_from_deck = ev->kind == EV_INPUT;
    input_only_ip = ev->only_ip;
    db_input_t out[EVENTS_PER_RPT];
    size_t n = k->dev.driver->parse_input(&k->dev, ev->report, ev->len, ev->t_us, out, EVENTS_PER_RPT);
    bool swallowed[EVENTS_PER_RPT];
    bool any = false;
    bool woke = false;
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&wake_mux);
    for (size_t e = 0; e < n; e++) {
        bool w;
        swallowed[e] = db_wake_input(&wake, deck, &out[e], now, &w);
        any |= swallowed[e];
        woke |= w;
    }
    taskEXIT_CRITICAL(&wake_mux);
    if (woke) {
        light_all(false);
    }
    for (int i = 0; i < s.listener_count && !any; i++) {
        if (s.listeners[i].raw_input && (ev->real || !s.listeners[i].external)) {
            s.listeners[i].raw_input(deck, raw, ev->len);
        }
    }
    for (size_t e = 0; e < n; e++) {
        for (int i = 0; i < s.listener_count && !swallowed[e]; i++) {
            if (s.listeners[i].input && (ev->real || !s.listeners[i].external)) {
                s.listeners[i].input(deck, &out[e]);
            }
        }
    }
}

static void event_task(void *arg)
{
    ev_t ev;
    int64_t counted = 0;
    for (;;) {
        bool got = xQueueReceive(s.events, &ev, pdMS_TO_TICKS(5000)) == pdTRUE;
        if (esp_timer_get_time() - counted >= 5000000) {
            counted = esp_timer_get_time();
            count_parked();
            unpark(); /* also after a cycle settled, or an open that failed */
        }
        if (!got) {
            continue;
        }
        switch (ev.kind) {
        case EV_ATTACHED:
            handle_attach(ev.address);
            break;
        case EV_DETACHED:
            handle_detach(ev.port);
            break;
        case EV_UNPARK:
            unpark();
            break;
        case EV_INPUT:
            if (s.port_deck[ev.port] >= 0 && decks[s.port_deck[ev.port]].present) {
                handle_input(s.port_deck[ev.port], &ev, ev.report);
            }
            break;
        case EV_SIMULATE: {
            /* built here, so it starts from the key states every earlier report left */
            deck_t *k = &decks[ev.deck];
            db_input_t in;
            memcpy(&in, ev.report, sizeof(in));
            uint8_t real_state[sizeof(k->sim_state)];
            memcpy(real_state, k->dev.state, sizeof(real_state));
            memcpy(k->dev.state, k->sim_state, sizeof(real_state));
            size_t len =
                k->present ? k->dev.driver->build_input(&k->dev, &in, ev.report, sizeof(ev.report)) : 0;
            /* dock hosts get whole-deck reports: with the keys a person holds on the deck as well */
            uint8_t raw[sizeof(ev.report)];
            memcpy(k->dev.state, real_state, sizeof(real_state));
            if (len && k->dev.driver->build_input(&k->dev, &in, raw, sizeof(raw)) == len) {
                for (size_t i = 0; i < len; i++) {
                    raw[i] |= ev.report[i];
                }
            } else {
                memcpy(raw, ev.report, sizeof(raw));
            }
            memcpy(k->dev.state, k->sim_state, sizeof(real_state));
            if (len) {
                ev.len = (uint8_t)len;
                handle_input(ev.deck, &ev, raw);
            }
            memcpy(k->sim_state, k->dev.state, sizeof(real_state));
            memcpy(k->dev.state, real_state, sizeof(real_state));
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

static void on_detached(int port)
{
    ev_t ev = {.kind = EV_DETACHED, .port = (int8_t)port};
    xQueueSend(s.events, &ev, pdMS_TO_TICKS(100));
}

static void on_input(int port, const uint8_t *report, size_t len, uint64_t t_us)
{
    ev_t ev = {.kind = EV_INPUT,
               .port = (int8_t)port,
               .real = true,
               .t_us = t_us,
               .len = (uint8_t)(len < INPUT_REPORT ? len : INPUT_REPORT)};
    memcpy(ev.report, report, ev.len);
    /* a lost unplug keeps the device open in usb_port for good */
    if (uxQueueSpacesAvailable(s.events) <= PLUG_ROOM || xQueueSend(s.events, &ev, 0) != pdTRUE) {
        s.stats.inputs_dropped++;
    }
}

size_t db_deck_input_space(void)
{
    return uxQueueSpacesAvailable(s.events);
}

bool db_deck_full_speed(void)
{
    return usb_port_full_speed();
}

bool db_deck_behind_hub(int deck)
{
    return decks[deck].present && usb_port_behind_hub(decks[deck].port);
}

esp_err_t db_deck_simulate(int deck, const db_input_t *in, bool real, uint32_t only_ip)
{
    _Static_assert(sizeof(db_input_t) <= INPUT_REPORT, "input does not fit the event");
    if (!decks[deck].present) {
        return ESP_ERR_INVALID_STATE;
    }
    ev_t ev = {.kind = EV_SIMULATE,
               .deck = (int8_t)deck,
               .real = real,
               .only_ip = only_ip,
               .t_us = (uint64_t)esp_timer_get_time()};
    memcpy(ev.report, in, sizeof(*in));
    return xQueueSend(s.events, &ev, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t db_deck_start(bool full_speed, int max_decks)
{
    s.max_decks = max_decks;
    db_wake_init(&wake, esp_timer_get_time());
    memset(s.port_deck, -1, sizeof(s.port_deck));
    nvs_handle_t h;
    if (nvs_open("decks", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s.owners);
        nvs_get_blob(h, "slots", s.owners, &len);
        nvs_close(h);
    }
    s.events = xQueueCreate(64, sizeof(ev_t));
    if (!s.events) {
        return ESP_ERR_NO_MEM;
    }
    db_screen_seed_ids(esp_random());
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        deck_t *k = &decks[d];
        k->lock = xSemaphoreCreateMutex();
        k->io_lock = xSemaphoreCreateMutex();
        k->shot_lock = xSemaphoreCreateMutex();
        k->lit = -1;
        for (int i = 0; i < DB_SOURCE_COUNT; i++) {
            db_screen_init(&k->screens[i], psram_realloc, heap_caps_free);
        }
        if (!k->lock || !k->io_lock || !k->shot_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xTaskCreatePinnedToCore(event_task, "deck_events", 6144, NULL, 7, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    usb_port_cb_t cb = {
        .attached = on_attached, .detached = on_detached, .input = on_input, .stuck = on_stuck};
    return usb_port_start(&cb, full_speed);
}

void db_deck_add_listener(const db_deck_listener_t *listener)
{
    configASSERT(s.listener_count < MAX_LISTENERS); /* one more silently missed all input once */
    s.listeners[s.listener_count++] = *listener;
}

void db_deck_set_ack_handler(db_source_t source, db_deck_ack_cb_t cb, void *arg)
{
    s.ack[source].cb = cb;
    s.ack[source].arg = arg;
}

void db_deck_set_writer(int deck, db_source_t source, bool keep_picture)
{
    deck_t *k = &decks[deck];
    xSemaphoreTake(k->lock, portMAX_DELAY);
    if (k->writer != source) {
        k->keep_picture = keep_picture;
        ESP_LOGI(TAG, "deck %d: writer %d -> %d", deck + 1, k->writer, source);
        if (source != DB_SOURCE_UI) {
            db_log_mark(DB_MARK_HOST_DRAWS);
        }
        k->writer = source;
        k->replay = true;
        db_regions_clear(&k->pending, free_region, (void *)&wrong_role);
    }
    xSemaphoreGive(k->lock);
    if (k->writer_task) {
        xTaskNotifyGive(k->writer_task);
    }
}

const char *db_deck_source_name(db_source_t source)
{
    static const char *const names[DB_SOURCE_COUNT] = {"ui",    "server1", "server2", "server3", "server4",
                                                       "dock1", "dock2",   "dock3",   "dock4",   "satellite"};
    return source < DB_SOURCE_COUNT ? names[source] : "";
}

static bool replugging;

/* arg: off_ms << 8 | USB address */
static void replug_task(void *arg)
{
    uintptr_t a = (uintptr_t)arg;
    esp_err_t err = usb_port_replug(a & 0xff, a >> 8);
    taskENTER_CRITICAL(&cycle_mux);
    settled = esp_timer_get_time() + 5000000;
    taskEXIT_CRITICAL(&cycle_mux);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reconnecting the deck failed: %s", esp_err_to_name(err));
    }
    __atomic_store_n(&replugging, false, __ATOMIC_RELAXED);
    vTaskDelete(NULL);
}

/* one at a time: a second would switch the port on in the middle of the first one's off time */
static bool start_replug(uint8_t address, uint32_t off_ms)
{
    if (__atomic_exchange_n(&replugging, true, __ATOMIC_RELAXED)) {
        return false;
    }
    if (xTaskCreate(replug_task, "deck_replug", 3072, (void *)((uintptr_t)off_ms << 8 | address), 5, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "no task to reconnect the deck");
        __atomic_store_n(&replugging, false, __ATOMIC_RELAXED);
        return false;
    }
    return true;
}

esp_err_t db_deck_stall(int deck)
{
    deck_t *k = &decks[deck];
    xSemaphoreTake(k->io_lock, portMAX_DELAY);
    esp_err_t err = k->present ? usb_port_stall(k->port) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(k->io_lock);
    return err;
}

bool db_deck_reconnect(uint32_t off_ms)
{
    ESP_LOGI(TAG, "reconnecting the decks, port off for %lu ms", (unsigned long)off_ms);
    return start_replug(0, off_ms);
}

void db_deck_forget(int deck, db_source_t source)
{
    deck_t *k = &decks[deck];
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    db_screen_clear(&k->screens[source]);
    k->feature[source].len = 0;
    k->bright[source] = 0; /* the next host in this slot sets its own */
    xSemaphoreGive(k->shot_lock);
}

int db_deck_brightness(int deck)
{
    return __atomic_load_n(&decks[deck].lit, __ATOMIC_RELAXED);
}

db_source_t db_deck_writer(int deck)
{
    return decks[deck].writer;
}

bool db_deck_info(int deck, db_device_info_t *out)
{
    deck_t *k = &decks[deck];
    xSemaphoreTake(k->lock, portMAX_DELAY);
    bool present = k->present;
    if (present) {
        *out = k->dev.info;
    }
    xSemaphoreGive(k->lock);
    return present;
}

bool db_deck_present(int deck)
{
    return decks[deck].present;
}

int db_deck_find(const char *serial)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (decks[d].present && strcmp(decks[d].dev.info.serial, serial) == 0) {
            return d;
        }
    }
    return -1;
}

int db_deck_first(void)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (decks[d].present) {
            return d;
        }
    }
    return -1;
}

void db_deck_set_max(int max_decks)
{
    ev_t ev = {.kind = EV_UNPARK};
    s.max_decks = max_decks;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        xSemaphoreTake(decks[d].lock, portMAX_DELAY);
        decks[d].pending.max_bytes = PENDING_BUDGET / max_decks; /* queued images over it stay until drawn */
        xSemaphoreGive(decks[d].lock);
    }
    xQueueSend(s.events, &ev, pdMS_TO_TICKS(100));
}

int db_deck_over_limit(void)
{
    return __atomic_load_n(&s.over, __ATOMIC_RELAXED);
}

static esp_err_t to_esp(db_err_t err)
{
    return err == DB_OK ? ESP_OK : ESP_FAIL;
}

/* The writer task sets it: the web server and the Satellite link must not wait for a hung deck. */
esp_err_t db_deck_set_brightness(int deck, uint8_t pct)
{
    deck_t *k = &decks[deck];
    if (!k->present) {
        return ESP_ERR_INVALID_STATE;
    }
    __atomic_store_n(&k->lit, pct,
                     __ATOMIC_RELAXED); /* as the deck will have it: a menu opening now reads it */
    __atomic_store_n(&k->brightness, 1, __ATOMIC_RELAXED);
    xTaskNotifyGive(k->writer_task);
    return ESP_OK;
}

/* An image report from a dock host is collected until its image is whole, and that goes to the queue like a
 * server's image: a newer image for the same key replaces one that is not drawn yet, so fast page flips do
 * not fall behind. */
esp_err_t db_deck_raw_write(int deck, db_source_t source, const uint8_t *report, size_t len)
{
    deck_t *k = &decks[deck];
    if (!k->present) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Wait 300 ms before the first host write after a deck is plugged in. */
    if (!k->host_wrote) {
        k->host_wrote = true;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    const db_hid_io_t *io = k->dev.io;
    uint8_t padded[1024] = {0}; /* hosts may send short reports; the endpoint wants full ones */
    memcpy(padded, report, len < io->out_report_size ? len : io->out_report_size);
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    db_chunk_t chunk;
    if (k->dev.driver->parse_output(&k->dev, padded, io->out_report_size, &chunk)) {
        if (!db_device_find_target(&k->dev.info, chunk.display,
                                   chunk.rect)) { /* a partial strip rect is the host's */
            xSemaphoreGive(k->shot_lock);
            if (chunk.part == 0) {
                ESP_LOGW(TAG, "%s: image at %u,%u %ux%u is off the display, dropped",
                         db_deck_source_name(source), chunk.rect.x, chunk.rect.y, chunk.rect.w, chunk.rect.h);
            }
            return ESP_OK;
        }
        db_screen_t *sc = &k->screens[source];
        db_err_t fed = db_screen_feed(sc, &chunk);
        if (fed != DB_OK) {
            ESP_LOGW(TAG, "%s: image at %u,%u dropped from the kept picture", db_deck_source_name(source),
                     chunk.rect.x, chunk.rect.y);
        }
        uint8_t *copy = NULL;
        db_shot_t whole = {0};
        if (fed == DB_OK && chunk.last && k->writer == source && sc->count) {
            whole = sc->shots[sc->count - 1]; /* the image the last piece completed */
            copy = heap_caps_malloc(whole.len, MALLOC_CAP_SPIRAM);
            if (copy) {
                memcpy(copy, whole.data, whole.len);
            }
        }
        xSemaphoreGive(k->shot_lock);
        if (copy) {
            db_deck_submit(deck, source, whole.display, whole.rect, 0, copy, whole.len,
                           (uint64_t)esp_timer_get_time());
        }
        return ESP_OK;
    }
    db_err_t err = DB_OK;
    if (k->writer == source) {
        xSemaphoreTake(k->io_lock, portMAX_DELAY); /* as in db_deck_raw_set_feature */
        xSemaphoreGive(k->shot_lock);
        err = io->write(io, padded, io->out_report_size);
        xSemaphoreGive(k->io_lock);
        return to_esp(err);
    }
    xSemaphoreGive(k->shot_lock);
    return to_esp(err);
}

esp_err_t db_deck_raw_get_feature(int deck, uint8_t *buf, size_t len, size_t *out_len)
{
    deck_t *k = &decks[deck];
    if (!k->present) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(k->io_lock, portMAX_DELAY);
    db_err_t err = k->dev.io->get_feature(k->dev.io, buf, len, out_len);
    xSemaphoreGive(k->io_lock);
    return to_esp(err);
}

esp_err_t db_deck_raw_set_feature(int deck, db_source_t source, const uint8_t *buf, size_t len)
{
    deck_t *k = &decks[deck];
    if (!k->present) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    db_err_t err = DB_OK;
    int pct = brightness_of(buf, len);
    if (pct >= 0) {
        k->bright[source] = (uint8_t)(pct > 100 ? 101 : pct + 1);
    }
    if (k->writer == source) {
        if (pct >= 0) {
            __atomic_store_n(&k->lit, pct, __ATOMIC_RELAXED); /* before dark(): a wake after it lights this */
        }
        /* the io lock before the shot lock goes: a writer change cannot slip in, and nobody who only reads
         * the picture waits for the USB transfer */
        xSemaphoreTake(k->io_lock, portMAX_DELAY);
        xSemaphoreGive(k->shot_lock);
        err = pct >= 0 && dark() ? DB_OK : k->dev.io->set_feature(k->dev.io, buf, len);
        xSemaphoreGive(k->io_lock);
        return to_esp(err);
    }
    if (pct < 0 && len <= sizeof(k->feature[source].buf)) {
        memcpy(k->feature[source].buf, buf, len);
        k->feature[source].len = len;
    }
    xSemaphoreGive(k->shot_lock);
    return to_esp(err);
}

size_t db_deck_shots(int deck, db_source_t *writer, db_shot_t *out, size_t max)
{
    deck_t *k = &decks[deck];
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    *writer = k->writer;
    const db_screen_t *sc = &k->screens[k->writer];
    size_t n = 0;
    for (int i = 0; i < sc->count && n < max; i++, n++) {
        out[n] = sc->shots[i];
        out[n].data = NULL;
    }
    xSemaphoreGive(k->shot_lock);
    return n;
}

static esp_err_t copy_shot(deck_t *k, uint32_t id, uint8_t **data, size_t *len, db_rect_t *rect)
{
    esp_err_t err = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(k->shot_lock, portMAX_DELAY);
    /* only who draws now: the menu's last picture (the web password) must not stay reachable by its id */
    const db_screen_t *sc = &k->screens[k->writer];
    for (int i = 0; i < sc->count; i++) {
        if (sc->shots[i].id == id) {
            *data = heap_caps_malloc(sc->shots[i].len, MALLOC_CAP_SPIRAM);
            err = *data ? ESP_OK : ESP_ERR_NO_MEM;
            if (*data) {
                memcpy(*data, sc->shots[i].data, sc->shots[i].len);
                *len = sc->shots[i].len;
                *rect = sc->shots[i].rect;
            }
            break;
        }
    }
    xSemaphoreGive(k->shot_lock);
    return err;
}

esp_err_t db_deck_shot_copy(int deck, uint32_t id, uint8_t **data, size_t *len)
{
    db_rect_t rect;
    return copy_shot(&decks[deck], id, data, len, &rect);
}

esp_err_t db_deck_shot_upright(int deck, uint32_t id, uint8_t **jpeg, size_t *len)
{
    deck_t *k = &decks[deck];
    db_display_t d = {0};
    xSemaphoreTake(k->lock, portMAX_DELAY);
    if (k->present) {
        d = k->dev.info.displays[0];
    }
    xSemaphoreGive(k->lock);
    uint8_t *data = NULL;
    size_t dlen = 0;
    db_rect_t r = {0};
    esp_err_t err = copy_shot(k, id, &data, &dlen, &r);
    if (err != ESP_OK || (!d.rotate && !d.flip_x && !d.flip_y && d.format == DB_FORMAT_JPEG)) {
        *jpeg = data;
        *len = dlen;
        return err;
    }
    uint8_t *px = jpeg_port_alloc_rgb(r.w, r.h);
    err =
        px ? jpeg_port_decode_for(&d, data, dlen, px, r.w, r.h, (db_rect_t){0, 0, r.w, r.h}) : ESP_ERR_NO_MEM;
    heap_caps_free(data);
    if (err == ESP_OK) {
        err = jpeg_port_encode(px, r.w, r.h, 80, jpeg, len);
    }
    heap_caps_free(px);
    return err;
}

void db_deck_stats(db_deck_stats_t *out)
{
    *out = s.stats;
    out->usb_errors = usb_port_errors();
    out->usb_recoveries = usb_port_recoveries();
}
