/* Stand-ins for everything the API layer calls on the chip: Stream Deck + decks (DB_SIM_USB picks another) that
 * are always attached (DB_SIM_DECKS of them, 1 by default, serials SIM0001 and up; the ones over max_decks stay
 * dark), no dock hosts, no updates. Simulated input goes to the listeners, so the web socket reports it like on
 * the device.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_driver.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_satellite.h"
#include "db_ui.h"
#include "db_wake.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define LISTENERS 8

static db_deck_listener_t listeners[LISTENERS];
static int listener_count;
static pthread_mutex_t wake_lock = PTHREAD_MUTEX_INITIALIZER;
static db_wake_t wake;
static int lit[DB_MAX_DECKS];
static int wake_pct = DB_WAKE_PCT;

void db_deck_add_listener(const db_deck_listener_t *l)
{
    if (listener_count < LISTENERS) {
        listeners[listener_count++] = *l;
    }
}

static int plugged(void)
{
    int want = getenv("DB_SIM_DECKS") ? atoi(getenv("DB_SIM_DECKS")) : 1;
    return want < 0 ? 0 : want > DB_MAX_DECKS ? DB_MAX_DECKS : want;
}

static int decks(void)
{
    static int n = -1;
    if (n < 0) {
        int max = db_config()->max_decks;
        n = plugged() < max ? plugged() : max; /* before db_deck_info asks for it */
        db_wake_init(&wake, esp_timer_get_time());
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            lit[d] = db_config()->brightness;
        }
        for (int d = 0; d < n; d++) { /* as the deck task tells them on the device */
            db_device_info_t info;
            db_deck_info(d, &info);
            for (int i = 0; i < listener_count; i++) {
                if (listeners[i].device_changed) {
                    listeners[i].device_changed(d, &info);
                }
            }
        }
    }
    return n;
}

int db_deck_over_limit(void)
{
    return plugged() - decks();
}

void db_deck_set_max(int max_decks)
{
}

int db_deck_first(void)
{
    return decks() ? 0 : -1;
}

int db_deck_find(const char *serial)
{
    for (int d = 0; d < decks(); d++) {
        db_device_info_t info;
        if (db_deck_info(d, &info) && strcmp(info.serial, serial) == 0) {
            return d;
        }
    }
    return -1;
}

static db_err_t sim_write(const db_hid_io_t *io, const uint8_t *report, size_t len)
{
    (void)io;
    (void)report;
    (void)len;
    return DB_OK;
}

static db_err_t sim_flush(const db_hid_io_t *io)
{
    (void)io;
    return DB_OK;
}

/* DB_SIM_USB=vid:pid (hex) plugs in that deck as its driver describes it */
static bool sim_usb(db_device_info_t *out)
{
    static const db_hid_io_t io = {.write = sim_write, .flush = sim_flush};
    unsigned vid = 0;
    unsigned pid = 0;
    const char *e = getenv("DB_SIM_USB");
    db_dev_t dev = {.io = &io};
    if (!e || sscanf(e, "%x:%x", &vid, &pid) != 2 || !(dev.driver = db_driver_find(vid, pid)) ||
        dev.driver->open(&dev, vid, pid) != DB_OK) {
        return false;
    }
    *out = dev.info;
    return true;
}

bool db_deck_info(int deck, db_device_info_t *out)
{
    if (deck >= decks()) {
        return false;
    }
    if (!sim_usb(out)) {
        *out = (db_device_info_t){.vendor = "Elgato",
                                  .model = "Stream Deck +",
                                  .vid = 0x0fd9,
                                  .pid = 0x0084,
                                  .dock = true,
                                  .surface = "streamdeck",
                                  .key_count = 8,
                                  .encoder_count = 4,
                                  .display_count = 1};
        out->displays[0].width = 800;
        out->displays[0].height = 480;
    }
    snprintf(out->serial, sizeof(out->serial), "SIM%04d", deck + 1);
    strcpy(out->firmware, "sim");
    return true;
}

static bool input_real;
static uint32_t input_only_ip;

bool db_deck_input_from_deck(void)
{
    return false; /* the simulator has no deck: every input comes over the API */
}

bool db_deck_input_real_for(uint32_t peer_ip)
{
    return input_real && (!input_only_ip || input_only_ip == peer_ip);
}

size_t db_deck_input_space(void)
{
    return 64;
}

esp_err_t db_deck_simulate(int deck, const db_input_t *in, bool real, uint32_t only_ip)
{
    if (deck >= decks()) {
        return ESP_ERR_INVALID_STATE;
    }
    bool woke;
    pthread_mutex_lock(&wake_lock);
    bool zero = lit[deck] == 0; /* the UI always draws here */
    bool swallowed = db_wake_input(&wake, deck, zero, false, in, esp_timer_get_time(), &woke);
    if (zero) {
        lit[deck] = wake_pct;
    }
    pthread_mutex_unlock(&wake_lock);
    input_real = real;
    input_only_ip = only_ip;
    int keeper = -1;
    for (int i = 0; i < listener_count && !swallowed && keeper < 0; i++) {
        keeper = listeners[i].keeps && listeners[i].keeps(deck, in) ? i : -1;
    }
    for (int i = 0; i < listener_count && !swallowed; i++) {
        if (listeners[i].input && (real || !listeners[i].external) && (keeper < 0 || keeper == i)) {
            listeners[i].input(deck, in);
        }
    }
    return ESP_OK;
}

bool db_deck_reconnect(uint32_t off_ms)
{
    return true;
}

esp_err_t db_deck_stall(int deck)
{
    return ESP_ERR_INVALID_STATE;
}
db_source_t db_deck_writer(int deck)
{
    return DB_SOURCE_UI;
}
const char *db_deck_source_name(db_source_t source)
{
    return source == DB_SOURCE_UI ? "ui" : "other";
}
esp_err_t db_deck_set_brightness(int deck, uint8_t pct)
{
    lit[deck] = pct;
    return ESP_OK;
}

bool db_deck_owned(int deck)
{
    return false;
}

int db_deck_brightness(int deck)
{
    return lit[deck];
}

int db_deck_shown(int deck)
{
    return db_wake_shown(wake.cap, lit[deck]);
}

void db_deck_screen_off(bool off)
{
    pthread_mutex_lock(&wake_lock);
    db_wake_set(&wake, off ? 0 : -1, esp_timer_get_time());
    pthread_mutex_unlock(&wake_lock);
}

void db_deck_screen_tick(int off_s, int dim, int brightness, bool covered)
{
    int cap;
    int64_t now = esp_timer_get_time();
    decks(); /* the wake state starts there */
    pthread_mutex_lock(&wake_lock);
    wake_pct = brightness > 0 ? brightness : DB_WAKE_PCT;
    if (covered) {
        db_wake_set(&wake, -1, now);
    } else if (db_wake_due(&wake, off_s, dim, false, now, &cap)) {
        db_wake_set(&wake, cap, now);
    }
    pthread_mutex_unlock(&wake_lock);
}

bool db_deck_screen_is_off(void)
{
    return wake.cap == 0;
}

size_t db_deck_shots(int deck, db_source_t *writer, db_shot_t *out, size_t max)
{
    *writer = DB_SOURCE_UI;
    return 0;
}

esp_err_t db_deck_shot_copy(int deck, uint32_t id, uint8_t **data, size_t *len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_deck_shot_upright(int deck, uint32_t id, uint8_t **jpeg, size_t *len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_dock_answer(bool accept)
{
    return ESP_OK;
}
void db_dock_pairing(int seconds)
{
}
bool db_dock_undeny(uint32_t ip)
{
    return false;
}
esp_err_t db_dock_forget(const char *ip)
{
    return ESP_OK;
}

esp_err_t db_ui_snapshot(int deck, uint8_t **jpeg, size_t *len)
{
    static const uint8_t tiny[] = {0xff, 0xd8, 0xff, 0xd9}; /* SOI, EOI: enough for a JPEG reply */
    *jpeg = malloc(sizeof(tiny));
    memcpy(*jpeg, tiny, sizeof(tiny));
    *len = sizeof(tiny);
    return ESP_OK;
}

bool db_ui_menu_chord(const db_device_info_t *dev, const char *chord, bool *on_encoders, int *last)
{
    (void)chord;
    *on_encoders = false;
    *last = db_device_image_keys(dev) - 1;
    return *last >= 1;
}

static bool menu_open;
static int64_t pass_until, reset_until, ask_until, ask_at;
static int ask_answer = -1;

static bool covered(void)
{
    int64_t now = esp_timer_get_time();
    return now < pass_until || now < reset_until || now < ask_until;
}

esp_err_t db_ui_open_menu(int deck)
{
    if (deck >= decks()) {
        return ESP_ERR_INVALID_STATE;
    }
    menu_open = true;
    return ESP_OK;
}

void db_ui_close_menu(void)
{
    menu_open = false;
    int64_t now = esp_timer_get_time();
    pass_until = pass_until < now ? pass_until : now;
    reset_until = reset_until < now ? reset_until : now;
    if (now < ask_until) {
        ask_answer = 0;
        ask_until = now;
    }
}

bool db_ui_menu_open(void)
{
    return menu_open || covered();
}

int db_ui_show_password(void)
{
    pass_until = esp_timer_get_time() + 30000000;
    return decks();
}

void db_ui_show_ca(const char *fingerprint)
{
    pass_until = esp_timer_get_time() + 30000000;
}

esp_err_t db_ui_factory_reset(void)
{
    return db_config_reset();
}

int db_ui_ask_reset(void)
{
    reset_until = esp_timer_get_time() + 60000000;
    return 60;
}

void db_ui_cancel_reset(void)
{
    reset_until = esp_timer_get_time();
}

bool db_ui_ask_login(uint32_t ip, const char *who, int seconds)
{
    ask_at = esp_timer_get_time();
    ask_until = ask_at + (int64_t)seconds * 1000000;
    ask_answer = -1;
    return true;
}

const char *db_ui_ask_companion(const char *addr, int seconds)
{
    return NULL;
}

/* DB_SIM_LOGIN_ACCEPT=1: someone at the deck accepts every request after 1 s. */
int db_ui_login_answer(void)
{
    if (ask_answer < 0 && esp_timer_get_time() < ask_until && getenv("DB_SIM_LOGIN_ACCEPT") &&
        esp_timer_get_time() - ask_at >= 1000000) {
        ask_answer = 1;
        ask_until = esp_timer_get_time();
    }
    return ask_answer;
}

void db_ui_end_login(void)
{
    ask_until = esp_timer_get_time() < ask_until ? esp_timer_get_time() : ask_until;
}

int64_t db_ui_page_end(db_ui_page_t page)
{
    return page == DB_UI_LOGIN ? ask_until : page == DB_UI_RESET ? reset_until : pass_until;
}

/* No servers and no Satellite client in the simulator: retries find nothing to wake. */
const char *db_link_url(int index)
{
    return "";
}

void db_link_retry(int index)
{
}

const char *db_satellite_address(void)
{
    return "";
}

void db_satellite_retry(void)
{
}

void db_satellite_moved(void)
{
}

bool db_satellite_pincode(int deck, int digit, bool real, uint32_t only_ip)
{
    return false;
}

bool db_ui_locked(int deck)
{
    return false;
}

const char *db_satellite_probe(const char *addr, const char *pin_hex, char *version, size_t cap)
{
    return "the simulator reaches no Companion";
}

bool db_ui_login_take(const char *token)
{
    static bool used; /* the simulator has no deck to show a code: this one works once */
    bool ok = !used && strcmp(token, "0123456789abcdef") == 0;
    used |= ok;
    return ok;
}

bool db_ui_login_shown(void)
{
    const db_config_t *c =
        db_config(); /* the deck's rule: a code until setup is done, the password, the setup WLAN */
    return (!c->setup_done && !c->qr_login_off) || esp_timer_get_time() < pass_until ||
           getenv("DB_SIM_VIA_AP");
}

bool db_ui_password_shown(void)
{
    return db_ui_login_shown() || !db_config()->setup_done || menu_open;
}

void db_ui_setup_step(int step)
{
}

bool db_dock_request(char ip[16])
{
    return false; /* no host asks in the simulator */
}
