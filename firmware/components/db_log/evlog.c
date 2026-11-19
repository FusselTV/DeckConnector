#include "db_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define PARTITION 0x41 /* data subtype of `evlog` in partitions.csv */
/* the settings storage also holds the settings: a flapping host must not wear it out */
#define NVS_EVERY_MS   30000
#define NVS_PER_HOUR   120
#define FLASH_EVERY_MS 1000
#define FLASH_PER_HOUR 600
#define TEXT_MAX       160

enum { K_BOOT, K_UPDATE, K_SOURCE, K_COMPANION, K_PAIRING, K_DECK, K_LAN, K_WLAN, K_MATTER, K_COUNT };
static const char *const kinds[K_COUNT] = {"boot", "update", "source", "companion", "pairing",
                                           "deck", "lan",    "wlan",   "matter"};
enum { L_INFO, L_OK, L_WARN, L_ERR };
static const char *const levels[] = {"info", "ok", "warn", "err"};

/* The English text is for the serial log, the download, Home Assistant and pages without a translation. {n}
 * is the n-th arg. */
static const struct {
    const char *id;
    uint8_t kind, level;
    const char *en;
} codes[DB_EV_COUNT] = {
    [DB_EV_TEXT] = {"text", K_BOOT, L_INFO, "{0}"},
    [DB_EV_MORE] = {"more", K_BOOT, L_INFO, "{0}"},
    [DB_EV_STARTED] = {"started", K_BOOT, L_INFO, "started ({0})"},
    [DB_EV_CRASHED] = {"crashed", K_BOOT, L_ERR, "started ({0})"},
    [DB_EV_SETTINGS_ERASED] =
        {"settings_erased", K_BOOT, L_ERR,
         "settings storage unreadable ({0}): erased, all settings are back to defaults"},
    [DB_EV_SETTINGS_FAILED] = {"settings_failed", K_BOOT, L_ERR,
                               "settings storage failed: {0}, running on defaults"},
    [DB_EV_START_FAILED] = {"start_failed", K_BOOT, L_ERR, "{0} failed to start: {1}"},
    [DB_EV_UPDATE_ABORTED] = {"update_aborted", K_UPDATE, L_ERR, "update aborted: {0}"},
    [DB_EV_UPDATE_INSTALLED] = {"update_installed", K_UPDATE, L_OK, "{0} update installed"},
    [DB_EV_UPDATE_RESTART] = {"update_restart", K_UPDATE, L_OK, "{0} update installed, restarting"},
    [DB_EV_UPDATE_CONFIRMED] = {"update_confirmed", K_UPDATE, L_OK, "new firmware confirmed"},
    [DB_EV_ROLLBACK_UNHEALTHY] = {"rollback_unhealthy", K_UPDATE, L_ERR,
                                  "new firmware never became healthy, rolling back"},
    [DB_EV_ROLLBACK_UNCONFIRMED] = {"rollback_unconfirmed", K_UPDATE, L_ERR,
                                    "the healthy new firmware could not be confirmed, rolling back"},
    [DB_EV_SERVER_LOST] = {"server_lost", K_SOURCE, L_WARN, "server {0} lost: {1}"},
    [DB_EV_SERVER_CONNECTED] = {"server_connected", K_SOURCE, L_OK, "server {0} connected over {1}: {2}"},
    [DB_EV_COMPANION_LOST] = {"companion_lost", K_SOURCE, L_WARN, "Companion lost: {0}"},
    [DB_EV_COMPANION_SILENT] = {"companion_silent", K_SOURCE, L_WARN, "Companion silent for {0} s"},
    [DB_EV_COMPANION_CONNECTED] = {"companion_connected", K_SOURCE, L_OK,
                                   "Companion connected at {0} over {1}"},
    [DB_EV_HOST_CONNECTED] = {"host_connected", K_SOURCE, L_OK, "{0} at {1} connected"},
    [DB_EV_HOST_LEFT] = {"host_left", K_SOURCE, L_INFO, "{0} left"},
    [DB_EV_DOCK_REFUSED] = {"dock_refused", K_SOURCE, L_WARN,
                            "Companion at {0} refused as a dock: it has the deck over Satellite"},
    [DB_EV_NO_SATELLITE] = {"no_satellite", K_SOURCE, L_WARN,
                            "Companion at {0} has no Satellite API; Satellite would be better"},
    [DB_EV_MOVED_TO_SATELLITE] = {"moved_to_satellite", K_SOURCE, L_OK,
                                  "Companion at {0} moved from Dock to Satellite"},
    [DB_EV_COMPANION_ASKS] = {"companion_asks", K_COMPANION, L_WARN, "Companion {0} asks for the deck"},
    [DB_EV_COMPANION_SETUP] = {"companion_setup", K_COMPANION, L_OK,
                               "Companion {0} set up over Satellite by its Discover"},
    [DB_EV_PAIR_ASKS] = {"pair_asks", K_PAIRING, L_WARN, "{0} asks to pair"},
    [DB_EV_PAIR_ACCEPTED] = {"pair_accepted", K_PAIRING, L_OK, "{0} accepted"},
    [DB_EV_PAIR_REFUSED] = {"pair_refused", K_PAIRING, L_ERR, "{0} refused"},
    [DB_EV_DECK_PARKED] = {"deck_parked", K_DECK, L_WARN,
                           "deck at USB address {0} left dark: max_decks is {1}"},
    [DB_EV_SCREEN_OFF] = {"screen_off", K_DECK, L_INFO, "screen off"},
    [DB_EV_SCREEN_DIMMED] = {"screen_dimmed", K_DECK, L_INFO, "screen dimmed"},
    [DB_EV_SCREEN_ON] = {"screen_on", K_DECK, L_INFO, "screen on"},
    [DB_EV_LAN_DEAD] = {"lan_dead", K_LAN, L_WARN, "LAN dead: the gateway stopped answering"},
    [DB_EV_LAN_BACK] = {"lan_back", K_LAN, L_OK, "LAN answers again"},
    [DB_EV_LAN_DOWN] = {"lan_down", K_LAN, L_ERR, "LAN link down"},
    [DB_EV_WLAN_BACK] = {"wlan_back", K_WLAN, L_OK, "WLAN {0} is back ({1} dBm), leaving the backup {2}"},
    /* the old access point last: it is the one cut when the args do not fit */
    [DB_EV_WLAN_ROAM] = {"wlan_roam", K_WLAN, L_INFO, "WLAN roam {0} {3} -> {1}, channel {2}"},
    [DB_EV_WLAN_JOINED] = {"wlan_joined", K_WLAN, L_OK, "WLAN joined {0} {1}, channel {2}"},
    [DB_EV_WLAN_NO_LINK] = {"wlan_no_link", K_WLAN, L_WARN, "WLAN {0} gives no link, trying {1}"},
    [DB_EV_WLAN_NO_LINK_BACKUP] = {"wlan_no_link_backup", K_WLAN, L_WARN,
                                   "WLAN {0} gives no link, trying the backup {1}"},
    [DB_EV_MATTER_PAIRED] = {"matter_paired", K_MATTER, L_OK, "a controller paired"},
    [DB_EV_MATTER_LEFT] = {"matter_left", K_MATTER, L_INFO, "a controller left"},
    [DB_EV_MATTER_STORAGE] = {"matter_storage", K_MATTER, L_ERR, "storage failed: {0}"},
    [DB_EV_MATTER_NO_CODE] = {"matter_no_code", K_MATTER, L_ERR, "no pairing code"},
    [DB_EV_MATTER_NO_MEMORY] = {"matter_no_memory", K_MATTER, L_ERR, "out of memory"},
    [DB_EV_MATTER_FAILED] = {"matter_failed", K_MATTER, L_ERR, "failed to start: {0}"},
};

/* How a firmware before event codes kept events in the settings storage, key "e<seq % DB_EVENTS_KEPT>". */
typedef struct {
    int64_t unix_s;
    uint32_t since_s;
    uint32_t seq;
    uint32_t boot;
    char kind[12];
    char text[84];
} legacy_t;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static db_event_t *ram; /* the newest DB_EVENTS; without evlog, the earlier boots' from settings come first */
static uint32_t n_ram;
static uint32_t next_seq = 1; /* renumbered by db_log_keep_start */
static uint32_t kept;         /* every event up to this one is stored or was skipped */
static uint32_t boot_no;
static SemaphoreHandle_t lock; /* the storage: the keeper writes, the web server reads */
static TaskHandle_t keeper;
static const esp_partition_t *part;
static db_evring_t ring;
static bool on_flash, on_nvs;
static void (*listener)(uint32_t seq);

static void english(const db_event_t *e, const char *more, char *out, size_t cap)
{
    const char *f = codes[e->code < DB_EV_COUNT ? e->code : DB_EV_TEXT].en;
    size_t n = 0;
    for (; *f && n + 1 < cap; f++) {
        if (f[0] == '{' && f[1] >= '0' && f[1] <= '9' && f[2] == '}') {
            const char *a = db_evrec_arg(e, f[1] - '0');
            n += (size_t)snprintf(out + n, cap - n, "%s%s", a ? a : "", f[1] == '0' && more ? more : "");
            n = n < cap ? n : cap - 1;
            f += 2;
        } else {
            out[n++] = *f;
        }
    }
    out[n] = '\0';
}

void db_log_event_args(const char *tag, db_ev_t code, const char *const *args)
{
    db_event_t e = {.since_s = (uint32_t)(esp_timer_get_time() / 1000000), .code = (uint8_t)code};
    e.kind = codes[code].kind;
    time_t now = time(NULL);
    e.unix_s = now > 1700000000 ? (uint32_t)now : 0; /* before SNTP the clock starts in 1970 */
    db_evrec_set_args(&e, args);
    char text[TEXT_MAX];
    english(&e, NULL, text, sizeof(text));
    ESP_LOGI(tag, "%s", text);
    if (!ram) { /* the first event comes from app_main before any other task */
        ram = heap_caps_calloc(DB_EVENTS, sizeof(*ram), MALLOC_CAP_SPIRAM);
    }
    if (!ram) {
        return;
    }
    portENTER_CRITICAL(&mux);
    e.seq = next_seq++;
    e.boot = (uint16_t)boot_no;
    ram[n_ram++ % DB_EVENTS] = e;
    portEXIT_CRITICAL(&mux);
    if (keeper) {
        xTaskNotifyGive(keeper);
    }
    if (listener) {
        listener(e.seq);
    }
}

static int fl_read(uint32_t off, void *buf, size_t len)
{
    return esp_partition_read(part, off, buf, len);
}

static int fl_write(uint32_t off, const void *buf, size_t len)
{
    return esp_partition_write(part, off, buf, len);
}

static int fl_erase(uint32_t off, size_t len)
{
    return esp_partition_erase_range(part, off, len);
}

static void nvs_key(char prefix, uint32_t seq, char *key)
{
    snprintf(key, 8, "%c%u", prefix, (unsigned)(seq % DB_EVENTS_KEPT));
}

/* Under `lock`, or before the keeper starts. `h` is the open "evlog" namespace, unused on flash. */
static bool store(nvs_handle_t h, db_event_t *e)
{
    if (on_flash) {
        return db_evring_append(&ring, e) == 0;
    }
    char key[8];
    nvs_key('r', e->seq, key);
    e->check = db_evrec_check(e);
    return nvs_set_blob(h, key, e, sizeof(*e)) == ESP_OK;
}

static int by_seq(const void *a, const void *b)
{
    uint32_t x = ((const db_event_t *)a)->seq;
    uint32_t y = ((const db_event_t *)b)->seq;
    return (x > y) - (x < y);
}

/* The events in the settings storage under `prefix`, oldest first. */
static size_t load_nvs(nvs_handle_t h, char prefix, db_event_t *out)
{
    size_t n = 0;
    for (uint32_t i = 0; i < DB_EVENTS_KEPT; i++) {
        char key[8];
        nvs_key(prefix, i, key);
        if (prefix == 'r') {
            size_t len = sizeof(*out);
            if (nvs_get_blob(h, key, &out[n], &len) == ESP_OK && len == sizeof(*out) &&
                out[n].check == db_evrec_check(&out[n])) {
                n++;
            }
            continue;
        }
        legacy_t k;
        size_t len = sizeof(k);
        if (nvs_get_blob(h, key, &k, &len) != ESP_OK || len != sizeof(k)) {
            continue;
        }
        k.text[sizeof(k.text) - 1] = k.kind[sizeof(k.kind) - 1] = '\0';
        uint8_t kind = K_BOOT;
        for (int c = 0; c < K_COUNT; c++) {
            kind = strcmp(k.kind, kinds[c]) == 0 ? (uint8_t)c : kind;
        }
        /* seq * 2 keeps the order; the text's rest follows as DB_EV_MORE at seq * 2 + 1 */
        db_event_t e = {.seq = k.seq * 2,
                        .unix_s = (uint32_t)k.unix_s,
                        .since_s = k.since_s,
                        .boot = (uint16_t)k.boot,
                        .code = DB_EV_TEXT,
                        .kind = kind};
        size_t head = sizeof(e.args) - 1;
        char first[sizeof(e.args)];
        snprintf(first, sizeof(first), "%.*s", (int)head, k.text);
        db_evrec_set_args(&e, (const char *const[]){first, NULL});
        out[n++] = e;
        if (strlen(k.text) > head) {
            e.seq++;
            e.code = DB_EV_MORE;
            db_evrec_set_args(&e, (const char *const[]){k.text + head, NULL});
            out[n++] = e;
        }
    }
    qsort(out, n, sizeof(*out), by_seq);
    return n;
}

/* Stores this boot's new events, oldest first. Past the hourly cap the rest is skipped for good, RAM only;
 * errors, updates and starts still count, so a restart's cause is never among them. */
static void keep_now(void)
{
    static int64_t hour_at;
    static int this_hour;
    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (now - hour_at > 3600LL * 1000000) {
        hour_at = now;
        this_hour = 0;
    }
    nvs_handle_t h = 0;
    bool ready = on_flash || (on_nvs && nvs_open("evlog", NVS_READWRITE, &h) == ESP_OK);
    int cap = on_flash ? FLASH_PER_HOUR : NVS_PER_HOUR;
    static bool warned;
    while (ready) {
        portENTER_CRITICAL(&mux);
        uint32_t from = n_ram > DB_EVENTS ? n_ram - DB_EVENTS : 0;
        bool more = false;
        db_event_t e = {0};
        for (uint32_t i = from; i < n_ram && !more; i++) {
            e = ram[i % DB_EVENTS];
            more = e.seq > kept;
        }
        portEXIT_CRITICAL(&mux);
        if (!more) {
            break;
        }
        const uint8_t c = e.code < DB_EV_COUNT ? e.code : DB_EV_TEXT;
        if (this_hour >= cap && codes[c].level != L_ERR && codes[c].kind != K_UPDATE &&
            codes[c].kind != K_BOOT) {
            kept = e.seq;
            if (!warned) { /* not db_log_event: it would wake this task again */
                ESP_LOGW("evlog", "more than %d events this hour, the rest stay in RAM", cap);
            }
            warned = true;
            continue;
        }
        if (!store(h, &e)) {
            ESP_LOGW("evlog", "event %u not stored, it stays in RAM", (unsigned)e.seq);
            if (!on_flash) {
                break; /* the settings storage is full: this event is tried again at the next wake-up */
            }
        }
        warned = false;
        kept = e.seq;
        this_hour++;
    }
    if (h) {
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(lock);
}

/* A flash write or erase stalls both CPUs for a moment: never inside db_log_event(), which USB and network
 * tasks call. */
static void keep_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        keep_now();
        vTaskDelay(pdMS_TO_TICKS(on_flash ? FLASH_EVERY_MS : NVS_EVERY_MS));
    }
}

void db_log_keep_start(void)
{
    db_event_t *old = heap_caps_malloc(DB_EVENTS_KEPT * 3 * sizeof(*old), MALLOC_CAP_SPIRAM);
    void *buf = heap_caps_malloc(DB_EVRING_ERASE, MALLOC_CAP_SPIRAM);
    if (!ram || !old || !buf || !(lock = xSemaphoreCreateMutex())) {
        ESP_LOGE("evlog", "out of memory, events stay in RAM");
        free(old);
        free(buf);
        return;
    }
    nvs_handle_t h = 0;
    bool nv = nvs_open("evlog", NVS_READWRITE, &h) == ESP_OK; /* the boot count and earlier events */
    if (nv) {
        nvs_get_u32(h, "boot", &boot_no);
    }
    boot_no++;
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION, "evlog");
    db_evflash_t f = {.read = fl_read, .write = fl_write, .erase = fl_erase, .size = part ? part->size : 0};
    int err = part ? db_evring_open(&ring, &f, buf) : 0;
    free(buf);
    on_flash = part && !err;
    db_event_t newest;
    /* the settings storage was erased but evlog was not: count on from the newest boot it holds */
    if (on_flash && ring.newest && db_evring_get(&ring, ring.newest, &newest) &&
        (int16_t)(uint16_t)(boot_no - newest.boot) <= 0) {
        boot_no = newest.boot + 1u;
    }
    if (nv) {
        nvs_set_u32(h, "boot", boot_no);
    }
    on_nvs = !on_flash && nv;
    if (err) {
        ESP_LOGE("evlog", "evlog partition: %s, events go to the settings storage", esp_err_to_name(err));
    }
    /* with evlog, all the settings storage holds moves there; without it, only what a firmware before event
     * codes left is converted. Either way they get numbers after the newest stored. */
    size_t n_legacy = nv ? load_nvs(h, 'e', old) : 0;
    size_t n_rec = nv ? load_nvs(h, 'r', old + n_legacy) : 0;
    uint32_t base = on_flash ? ring.next : n_rec ? old[n_legacy + n_rec - 1].seq + 1 : 1;
    size_t n_move = on_flash ? n_legacy + n_rec : on_nvs ? n_legacy : 0;
    size_t moved = 0;
    for (size_t i = 0; i < n_move; i++) {
        old[i].seq = base++;
        moved += store(h, &old[i]);
    }
    for (uint32_t i = 0; n_move && moved == n_move && i < DB_EVENTS_KEPT; i++) {
        char key[8];
        nvs_key('e', i, key);
        nvs_erase_key(h, key);
        nvs_key('r', i, key);
        if (on_flash) {
            nvs_erase_key(h, key);
        }
    }
    if (moved < n_move) {
        ESP_LOGE("evlog", "%u of %u earlier events not moved, kept where they are",
                 (unsigned)(n_move - moved), (unsigned)n_move);
    }
    if (nv) {
        nvs_commit(h);
        nvs_close(h);
    }
    if (on_flash) {
        ESP_LOGI("evlog", "events %u to %u in flash, %u moved from the settings storage",
                 (unsigned)ring.oldest, (unsigned)ring.newest, (unsigned)moved);
    }
    /* without evlog, pages read only RAM: the last DB_EVENTS_KEPT go in front of this boot's */
    size_t n_all = n_legacy + n_rec;
    qsort(old, n_all, sizeof(*old), by_seq);
    size_t n_front = on_nvs ? (n_all < DB_EVENTS_KEPT ? n_all : DB_EVENTS_KEPT) : 0;
    portENTER_CRITICAL(&mux);
    /* ponytail: assumes fewer than DB_EVENTS - DB_EVENTS_KEPT events before this point (1 to 3 today) */
    uint32_t mine = n_ram < DB_EVENTS - n_front ? n_ram : (uint32_t)(DB_EVENTS - n_front);
    memmove(ram + n_front, ram, mine * sizeof(*ram));
    memcpy(ram, old + n_all - n_front, n_front * sizeof(*ram));
    for (uint32_t i = 0; i < mine; i++) {
        ram[n_front + i].seq = base + i;
        ram[n_front + i].boot = (uint16_t)boot_no;
    }
    n_ram = (uint32_t)n_front + mine;
    next_seq = base + mine;
    kept = base - 1;
    portEXIT_CRITICAL(&mux);
    free(old);
    xTaskCreatePinnedToCore(keep_task, "evlog", 3072, NULL, 1, &keeper, portNUM_PROCESSORS - 1);
    xTaskNotifyGive(keeper);                 /* this boot's first events, logged before storage was ready */
    esp_register_shutdown_handler(keep_now); /* a restart's last words, "update installed" and the like */
}

bool db_log_event_get(uint32_t seq, db_event_t *out)
{
    bool found = false;
    portENTER_CRITICAL(&mux);
    uint32_t from = n_ram > DB_EVENTS ? n_ram - DB_EVENTS : 0;
    for (uint32_t i = n_ram; ram && i-- > from && !found;) {
        if (ram[i % DB_EVENTS].seq == seq) {
            *out = ram[i % DB_EVENTS];
            found = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    if (!found && on_flash) {
        xSemaphoreTake(lock, portMAX_DELAY);
        found = db_evring_get(&ring, seq, out);
        xSemaphoreGive(lock);
    }
    return found;
}

void db_log_events_range(uint32_t *oldest, uint32_t *newest)
{
    portENTER_CRITICAL(&mux);
    uint32_t from = n_ram > DB_EVENTS ? n_ram - DB_EVENTS : 0;
    *oldest = ram && n_ram ? ram[from % DB_EVENTS].seq : 0;
    *newest = ram && n_ram ? ram[(n_ram - 1) % DB_EVENTS].seq : 0;
    portEXIT_CRITICAL(&mux);
    if (on_flash && ring.oldest) {
        *oldest = !*oldest || ring.oldest < *oldest ? ring.oldest : *oldest;
        *newest = ring.newest > *newest ? ring.newest : *newest;
    }
}

const char *db_log_events_store(void)
{
    return on_flash ? "flash" : on_nvs ? "settings" : "ram";
}

void db_log_on_event(void (*cb)(uint32_t seq))
{
    listener = cb;
}

void db_log_event_json(db_json_t *j, const char *key, const db_event_t *e)
{
    uint8_t code = e->code < DB_EV_COUNT ? e->code : DB_EV_TEXT;
    db_event_t rest;
    const char *more = code == DB_EV_TEXT && db_log_event_get(e->seq + 1, &rest) && rest.code == DB_EV_MORE
                           ? db_evrec_arg(&rest, 0)
                           : NULL;
    char text[TEXT_MAX];
    english(e, more, text, sizeof(text));
    db_json_obj_open(j, key);
    db_json_int(j, "seq", e->seq);
    if (e->unix_s) {
        db_json_int(j, "at", e->unix_s);
    } else {
        db_json_null(j, "at");
    }
    db_json_int(j, "since_s", e->since_s);
    db_json_int(j, "boot", (uint16_t)(boot_no - e->boot));
    uint8_t kind = code == DB_EV_TEXT ? e->kind : codes[code].kind; /* a legacy text keeps the kind it had */
    db_json_str(j, "kind", kinds[kind < K_COUNT ? kind : K_BOOT]);
    db_json_str(j, "code", codes[code].id);
    db_json_str(j, "level", levels[codes[code].level]);
    db_json_arr_open(j, "args");
    if (code == DB_EV_TEXT) {
        db_json_str(j, NULL, text);
    } else {
        for (int i = 0; db_evrec_arg(e, i); i++) {
            db_json_str(j, NULL, db_evrec_arg(e, i));
        }
    }
    db_json_arr_close(j);
    db_json_str(j, "text", text);
    db_json_obj_close(j);
}
