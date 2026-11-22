#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_audio.h"
#include "db_board.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_json.h"
#include "db_link.h"
#include "db_log.h"
#include "db_matter.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_satellite.h"
#include "db_serial.h"
#include "db_ui.h"
#include "db_web.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define SETUP_AP_AFTER_S 60
#define SETUP_LINK_S     5

static const char *TAG = "main";

static void write_device_fields(db_json_t *j, const db_device_info_t *dev)
{
    db_json_str(j, "model", dev->model);
    db_json_str(j, "serial", dev->serial);
    db_json_str(j, "firmware", dev->firmware);
    db_json_bool(j, "dock", dev->dock);
    db_json_int(j, "keys", dev->key_count);
    db_json_int(j, "encoders", dev->encoder_count);
    db_json_int(j, "width", dev->displays[0].width);
    db_json_int(j, "height", dev->displays[0].height);
    db_json_bool(j, "touch", dev->touch_count > 0);
    if (dev->tap_keys || dev->tap_encoders) { /* the web page's menu_chord rule knows only Elgato decks */
        static const char *const chords[] = {"auto", "keys", "encoders"};
        db_json_obj_open(j, "chords");
        for (int i = 0; i < 3; i++) {
            bool on_encoders;
            int last;
            bool any = db_ui_menu_chord(dev, chords[i], &on_encoders, &last);
            db_json_obj_open(j, chords[i]);
            db_json_str(j, "on", !any ? "off" : on_encoders ? "encoders" : "keys");
            db_json_int(j, "last", any ? last : 0);
            db_json_obj_close(j);
        }
        db_json_obj_close(j);
    }
}

/* `decks` lists every attached deck by slot (`?deck=<serial>` picks one); `device` is the first. */
static void write_device(db_json_t *j)
{
    /* too big for the callers' stacks, and not static: the web server, the web socket and the links call this
     * from their own tasks */
    db_device_info_t *dev = malloc(sizeof(*dev));
    if (!dev) {
        ESP_LOGE(TAG, "no memory for the device info");
        return; /* no "device" at all: null means no deck */
    }
    int first = db_deck_first();
    db_json_arr_open(j, "decks");
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (db_deck_info(d, dev)) {
            db_json_obj_open(j, NULL);
            db_json_int(j, "slot", d);
            db_json_int(j, "number", d + 1); /* as before slots: read by scripts */
            write_device_fields(j, dev);
            db_json_str(j, "usb_speed", db_deck_full_speed() ? "full" : "high");
            db_json_bool(j, "hub", db_deck_behind_hub(d));
            db_json_str(j, "writer", db_deck_source_name(db_deck_writer(d)));
            db_json_int(j, "brightness", db_deck_shown(d));
            db_sat_pin_t pin;
            bool locked = db_ui_locked(d) && db_satellite_locked(d, &pin);
            db_json_bool(j, "locked", locked);
            if (locked) {
                db_json_int(j, "pin_count", pin.count);
                db_json_bool(j, "pin_wrong", pin.wrong);
            }
            db_json_obj_close(j);
        }
    }
    db_json_arr_close(j);
    db_json_int(j, "decks_over_limit", db_deck_over_limit());
    if (first >= 0 && db_deck_info(first, dev)) {
        db_json_obj_open(j, "device");
        write_device_fields(j, dev);
        db_json_obj_close(j);
    } else {
        db_json_null(j, "device");
    }
    free(dev);
}

static void write_network(db_json_t *j, bool live)
{
    db_net_status_t n;
    db_net_status(&n);
    db_json_obj_open(j, "network");
    db_json_obj_open(j, "lan");
    db_json_bool(j, "cable", n.lan_link);
    db_json_bool(j, "dead", n.lan_dead);
    db_json_str(j, "ip", n.lan_up ? n.lan_ip : "");
    db_json_obj_close(j);
    db_json_obj_open(j, "wlan");
    db_json_str(j, "ssid", n.wlan_up ? n.wlan_ssid : db_config()->wifi_ssid);
    db_json_bool(j, "backup", n.wlan_up && n.wlan_backup);
    db_json_str(j, "ip", n.wlan_up ? n.wlan_ip : "");
    db_json_str(j, "bssid", n.wlan_up ? n.wlan_bssid : "");
    db_json_int(j, "channel", n.wlan_up ? n.wlan_channel : 0);
    db_json_str(j, "lock", db_config()->wifi_bssid);
    if (live) {
        db_json_int(j, "rssi", n.wlan_up ? n.rssi : 0);
    }
    db_json_str(j, "join", n.join_state ? n.join_state : "");
    db_json_str(j, "join_error", n.join_error);
    db_json_obj_close(j);
    db_json_obj_open(j, "ap");
    db_json_bool(j, "on", n.ap_active);
    db_json_str(j, "ssid", n.ap_active ? n.ap_ssid : "");
    db_json_obj_close(j);
    db_json_obj_close(j);
}

static void write_source(db_json_t *j, const char *kind, const char *host, uint32_t ip, bool connected,
                         int deck, int source, const db_health_t *health)
{
    char name[40] = "";
    if (ip) {
        db_dock_name(ip, name);
    }
    db_json_obj_open(j, NULL);
    db_json_str(j, "kind", kind);
    db_json_str(j, "host", host);
    db_json_str(j, "name", name);
    if (source == DB_SOURCE_SAT) {
        db_json_str(j, "via", "satellite");
    }
    db_json_bool(j, "connected", connected);
    if (health) { /* a connection the bridge makes: why it is down, since when, over which interface */
        db_json_str(j, "error", connected ? "" : health->error);
        db_json_int(j, "since_s", health->since_us / 1000000); /* after the start: an age would change the
                                                                live state every second */
        db_json_str(j, "iface", connected ? (health->over_wlan ? "wlan" : "lan") : "");
    }
    db_json_int(j, "deck", deck); /* the slot whose writer it is */
    db_json_bool(j, "chosen", source >= 0 && (db_source_t)source == db_ui_chosen_source(deck));
    db_json_bool(j, "drawing", connected && source >= 0 && (db_source_t)source == db_deck_writer(deck));
    db_json_obj_close(j);
}

/* Every server that is set or was found, Companion's Satellite API when set, and every Network Dock host. */
static void write_sources(db_json_t *j, bool live)
{
    db_json_arr_open(j, "sources");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (db_link_url(i)[0]) {
            db_health_t h = db_link_health(i);
            write_source(j, "server", db_link_url(i), 0, db_link_connected(i), db_link_deck(),
                         db_link_connected(i) ? DB_SOURCE_LINK0 + i : -1, &h);
        }
    }
    if (db_satellite_address()[0]) {
        db_health_t h = db_satellite_health();
        write_source(j, "companion", db_satellite_address(), db_satellite_peer(), db_satellite_connected(-1),
                     db_link_deck(), DB_SOURCE_SAT, &h);
    }
    db_dock_host_t hosts[DB_DOCK_HOSTS * DB_MAX_DECKS];
    size_t n = db_dock_hosts(-1, hosts, DB_DOCK_HOSTS * DB_MAX_DECKS);
    for (size_t i = 0; i < n; i++) {
        const char *kind = hosts[i].kind == DB_CTRL_SERVER ? "unknown" : db_controller_name(hosts[i].kind);
        write_source(j, kind, hosts[i].ip, inet_addr(hosts[i].ip), hosts[i].source >= 0, hosts[i].deck,
                     hosts[i].source, NULL);
    }
    db_json_arr_close(j);
    char ip[16];
    db_dock_peer_t known[8];
    size_t n_known = db_dock_known(known, 8);
    db_json_obj_open(j, "pairing");
    db_json_bool(j, "only_paired", db_config()->dock_paired_only);
    char name[40];
    if (db_dock_request(ip)) {
        db_json_str(j, "request", ip);
        db_dock_name(inet_addr(ip), name);
        db_json_str(j, "request_name", name);
    } else {
        db_json_null(j, "request");
    }
    db_json_bool(j, "open", db_dock_pairing_left() > 0);
    db_json_bool(j, "auto_open", n_known == 0); /* db_dock lets the first host in without pairing */
    if (live) { /* counts down every second: not in the web socket's state, which goes out on changes */
        db_json_int(j, "open_s", db_dock_pairing_left());
    }
    db_dock_denied_t denied[4];
    size_t n_denied = db_dock_denied(denied, 4);
    db_json_arr_open(j, "denied");
    for (size_t i = 0; i < n_denied; i++) {
        inet_ntoa_r(*(struct in_addr *)&denied[i].ip, ip, sizeof(ip));
        db_json_obj_open(j, NULL);
        db_json_str(j, "ip", ip);
        db_dock_name(denied[i].ip, name);
        db_json_str(j, "name", name);
        db_json_int(j, "since_s",
                    denied[i].at_us / 1000000); /* seconds after the start, like sources[].since_s */
        db_json_obj_close(j);
    }
    db_json_arr_close(j);
    db_json_arr_open(j, "hosts");
    for (size_t i = 0; i < n_known; i++) {
        const db_dock_peer_t *p = &known[i];
        char mac[18] = "";
        if (p->mac[0] | p->mac[1] | p->mac[2] | p->mac[3] | p->mac[4] | p->mac[5]) {
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", p->mac[0], p->mac[1], p->mac[2],
                     p->mac[3], p->mac[4], p->mac[5]);
        }
        inet_ntoa_r(*(struct in_addr *)&p->ip, ip, sizeof(ip));
        db_json_obj_open(j, NULL);
        db_json_str(j, "ip", ip);
        db_json_str(j, "mac", mac);
        db_dock_name(p->ip, name);
        db_json_str(j, "name", name);
        db_json_str(j, "kind",
                    p->kind == DB_CTRL_SERVER ? "unknown" : db_controller_name((db_controller_t)p->kind));
        db_json_bool(j, "connected", db_dock_peer_connected(p));
        db_json_obj_close(j);
    }
    db_json_arr_close(j);
    db_json_obj_close(j);
}

static const char *boot_reason(void)
{
    static const char *const reasons[] = {
        [ESP_RST_POWERON] = "power", [ESP_RST_EXT] = "pin",           [ESP_RST_SW] = "restart",
        [ESP_RST_PANIC] = "crash",   [ESP_RST_INT_WDT] = "watchdog",  [ESP_RST_TASK_WDT] = "watchdog",
        [ESP_RST_WDT] = "watchdog",  [ESP_RST_BROWNOUT] = "brownout",
    };
    esp_reset_reason_t r = esp_reset_reason();
    return r < sizeof(reasons) / sizeof(*reasons) && reasons[r] ? reasons[r] : "other";
}

static void write_boot(db_json_t *j)
{
    db_json_obj_open(j, "boot");
    db_json_str(j, "reason", boot_reason());
    static int64_t at; /* worked out once: now minus uptime jitters by a second, and the state would change */
    if (!at && time(NULL) > 1700000000) { /* before SNTP sets the clock it counts from 1970 */
        at = time(NULL) - esp_timer_get_time() / 1000000;
    }
    if (at) {
        db_json_int(j, "at", at);
    } else {
        db_json_null(j, "at");
    }
    db_json_obj_close(j);
}

/* Shared body of the server `status` message, /api/status and the web socket state. Without `live` it leaves
 * out what changes all the time (uptime, signal, counters), so a change in it is worth telling. */
static void write_status(db_json_t *j, bool live)
{
    db_deck_stats_t st;
    db_deck_stats(&st);
    const char *c6 = db_ota_c6_version();
    char id[13];
    db_config_board_id(id);
    db_json_str(j, "id", id); /* stable, unlike the name: for Home Assistant's unique_id */
    db_json_str(j, "https_fingerprint", db_web_fingerprint());
    db_json_bool(j, "https_custom", db_web_cert_custom());
    db_json_str(j, "https_ca", db_web_ca_short());
    db_json_bool(j, "https_custom_unread", db_web_cert_unread());
    db_json_int(j, "api", DB_WEB_API);
    db_json_str(j, "simulated_input", "local"); /* tests refuse to press keys on firmware without the gate */
    db_json_str(j, "name", db_config()->name);
    db_json_str(j, "firmware", esp_app_get_description()->version);
    if (c6) {
        db_json_str(j, "wlan_firmware", c6);
    } else {
        db_json_null(j, "wlan_firmware");
    }
    if (live) {
        db_json_int(j, "uptime_s", esp_timer_get_time() / 1000000);
    }
    write_boot(j);
    db_json_obj_open(j, "setup");
    db_json_bool(j, "active", !db_config()->setup_done || db_web_setup_step());
    db_json_int(j, "step", db_web_setup_step());
    db_json_bool(j, "rerun", db_config()->setup_done && db_web_setup_step());
    db_json_obj_close(j);
    db_json_str(j, "controller", db_controller_name(db_config()->controller));
    int first = db_deck_first();
    db_json_str(j, "writer", db_deck_source_name(first >= 0 ? db_deck_writer(first) : DB_SOURCE_UI));
    db_json_bool(j, "menu_open", db_ui_menu_open());
    db_json_bool(j, "screen_off", db_deck_screen_is_off());
    db_json_int(j, "brightness", db_config()->brightness);
    db_json_int(j, "volume", db_config()->volume);
    db_json_int(j, "screen_off_s", (int)db_config()->screen_off_s);
    db_json_int(j, "screen_dim", db_config()->screen_dim);
    db_json_int(j, "max_decks", db_config()->max_decks);
    db_json_bool(j, "speaker", db_board()->speaker.present && db_config()->speaker);
    write_device(j);
    write_network(j, live);
    write_sources(j, live);
    db_matter_status(j);
    if (!live) {
        return;
    }
    db_json_obj_open(j, "counters");
    db_json_int(j, "images", st.images);
    db_json_int(j, "superseded", st.superseded);
    db_json_int(j, "rejected", st.rejected);
    db_json_int(j, "usb_errors", st.usb_errors);
    db_json_int(j, "usb_recoveries", st.usb_recoveries);
    db_json_str(j, "usb_speed", db_deck_full_speed() ? "full" : "high");
    db_json_int(j, "inputs", st.inputs);
    db_json_int(j, "inputs_dropped", st.inputs_dropped + db_link_inputs_dropped());
    db_json_obj_close(j);
}

static size_t tagged_status(char *buf, size_t cap, const char *t, bool live)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", t);
    write_status(&j, live);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t link_status(char *buf, size_t cap)
{
    return tagged_status(buf, cap, "status", true);
}

/* lwIP numbers its sockets from LWIP_SOCKET_OFFSET; a closed one answers EBADF. */
static int open_sockets(void)
{
    int n = 0;
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; fd++) {
        n += fcntl(fd, F_GETFL, 0) >= 0;
    }
    return n;
}

static size_t web_status(char *buf, size_t cap)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    write_status(&j, true);
    db_json_obj_open(&j, "boot_ms");
    for (int m = 0; m < DB_MARK_COUNT; m++) {
        int64_t at = db_log_mark_ms((db_mark_t)m);
        if (at) {
            db_json_int(&j, db_log_mark_name((db_mark_t)m), at);
        }
    }
    db_json_obj_close(&j);
    db_json_obj_open(&j, "memory");
    db_json_int(&j, "heap_free", (int64_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    db_json_int(&j, "heap_min_free", (int64_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    db_json_int(&j, "psram_free", (int64_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    /* the largest block shows fragmentation, the task count a leak of tasks: both for long runs */
    db_json_int(&j, "heap_largest", (int64_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    /* LP RAM counts as internal heap; the log ring lives there, 16 KB that are not HP RAM */
    db_json_int(&j, "heap_lp_free", (int64_t)heap_caps_get_free_size(MALLOC_CAP_RTCRAM));
    db_json_int(&j, "psram_largest", (int64_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    db_json_int(&j, "tasks", uxTaskGetNumberOfTasks());
    db_json_int(&j, "sockets", open_sockets());
    db_json_int(&j, "sockets_max", CONFIG_LWIP_MAX_SOCKETS);
    db_json_obj_close(&j);
    if (db_web_temp_c()) {
        db_json_int(&j, "temp_c", db_web_temp_c());
    }
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t web_state(char *buf, size_t cap)
{
    return tagged_status(buf, cap, "state", false);
}

static void ota_broadcast(const char *target, const char *state, int pct, const char *error)
{
    char buf[128];
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", "ota");
    db_json_str(&j, "target", target);
    db_json_str(&j, "state", state);
    db_json_int(&j, "pct", pct);
    if (error) {
        db_json_str(&j, "error", error);
        if (db_web_error_code(error)) {
            db_json_str(&j, "code", db_web_error_code(error));
        }
    }
    db_json_obj_close(&j);
    if (db_json_end(&j)) {
        db_link_broadcast(buf);
        db_web_broadcast(buf);
    }
}

static void on_ota_progress(const char *target, const char *state, int pct, const char *error)
{
    ota_broadcast(target, state, pct, error);
    bool over = strcmp(state, "error") == 0 || (strcmp(state, "done") == 0 && !db_ota_restarts());
    db_ui_set_update(over ? -1 : pct, target[0] == 'c');
}

static void on_ota_requested(const char *target, const char *url)
{
    if (db_ota_start_url(url) != ESP_OK) { /* the update that runs keeps the deck */
        ota_broadcast(target, "error", -1, "busy");
    }
}

static char applied_sat[sizeof(db_config()->satellite) + sizeof(db_config()->satellite_pin)];

static void satellite_now(char *out)
{
    snprintf(out, sizeof(applied_sat), "%s %s", db_config()->satellite, db_config()->satellite_pin);
}

/* Ready once per start, when a host first draws; then lost when no host is there for 3 s (a menu or an update
 * over the picture does not count) and connected when one is back. */
static void sound_watch(void *arg)
{
    static bool ready, lost;
    static int gone_s;
    if (!ready) {
        if ((ready = db_log_mark_ms(DB_MARK_HOST_DRAWS) != 0)) {
            db_audio_play(DB_SOUND_READY);
        }
        return;
    }
    int first = db_deck_first();
    bool host = first >= 0 && db_ui_chosen_source(first) != DB_SOURCE_UI;
    gone_s = host ? 0 : gone_s + 1;
    if (host && lost) {
        lost = false;
        db_audio_play(DB_SOUND_CONNECTED);
    } else if (gone_s == 3) {
        lost = true;
        db_audio_play(DB_SOUND_LOST);
    }
}

static void apply_controller(void)
{
    db_link_reload();
    char sat_now[sizeof(applied_sat)];
    satellite_now(sat_now);
    if (strcmp(applied_sat, sat_now) != 0) {
        strcpy(applied_sat, sat_now);
        db_satellite_moved();
    }
    db_satellite_retry(); /* a new address is tried at once */
    db_dock_drop_satellite_host();
    /* also when only the host changed: another server or dock host of the same kind draws now */
    static char applied[sizeof(db_config()->host) + 4] = "-";
    char now[sizeof(applied)];
    db_controller_t c = db_config()->controller;
    snprintf(now, sizeof(now), "%d %s", c, db_config()->host);
    if (strcmp(now, applied) != 0) {
        strcpy(applied, now);
        ESP_LOGI(TAG, "controller: %s %s", db_controller_name(c), db_config()->host);
        db_link_set_enabled(c == DB_CTRL_SERVER);
    }
    db_deck_set_max(db_config()->max_decks);
    db_ui_refresh(); /* also for setup_done and qr_login, which change what the deck shows */
}

/* A path that came back: links waiting out their backoff try again at once. */
static void on_net(void)
{
    static unsigned was;
    db_net_status_t n;
    db_net_status(&n);
    unsigned up = (n.lan_up && !n.lan_dead) | n.wlan_up << 1;
    if (up & ~was) {
        for (int i = 0; i < DB_MAX_SERVERS; i++) {
            db_link_retry(i);
        }
        db_satellite_retry();
    }
    was = up;
    db_ui_refresh();
}

static void on_satellite(void)
{
    db_ui_hosts_changed();
    db_dock_drop_satellite_host(); /* the peer of a new session, if it is a dock host as well */
}

/* A new bridge without any network opens the setup WLAN by itself: at once when it has neither a cable link
 * nor a WLAN to join, else after SETUP_AP_AFTER_S if neither worked. It closes once the network is there and
 * no phone is on it, or when the page on it closes it after the join. */
static void setup_ap_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(SETUP_LINK_S * 1000)); /* time for a cable to show its link */
    db_net_status_t st;
    db_net_status(&st);
    if (st.lan_link || db_config()->wifi_ssid[0]) {
        vTaskDelay(pdMS_TO_TICKS((SETUP_AP_AFTER_S - SETUP_LINK_S) * 1000));
    }
    if (!db_config()->setup_done && !db_net_online() && db_net_ap_start() == ESP_OK) {
        ESP_LOGW(TAG, "no network, opening the setup WLAN");
        db_net_status_t n;
        do {
            vTaskDelay(pdMS_TO_TICKS(5000));
            db_net_status(&n);
        } while (n.ap_active && (!db_net_online() || db_net_ap_clients() > 0));
        if (n.ap_active) {
            ESP_LOGI(TAG, "network is up, closing the setup WLAN");
            db_net_ap_stop();
        }
    }
    vTaskDelete(NULL);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        db_log_event(TAG, DB_EV_SETTINGS_ERASED, esp_err_to_name(err));
        err = nvs_flash_erase();
        err = err == ESP_OK ? nvs_flash_init() : err;
    }
    if (err != ESP_OK) {
        db_log_event(TAG, DB_EV_SETTINGS_FAILED, esp_err_to_name(err));
    }
}

/* A part that fails to start leaves the rest running: a restart would fail the same way, again and again. A
 * new image that misses the network or the web server is never confirmed and goes back by itself. */
static void started(const char *what, esp_err_t err)
{
    if (err != ESP_OK) {
        db_log_event(TAG, DB_EV_START_FAILED, what, esp_err_to_name(err));
    }
}

static bool healthy(void)
{
    return db_net_online() && db_web_alive();
}

void app_main(void) /* NOLINT(misc-use-internal-linkage): ESP-IDF calls it */
{
#ifdef DB_TEST_CRASH
    abort();
#endif
    db_log_start();
    const char *reason = boot_reason();
    bool crashed =
        strcmp(reason, "crash") == 0 || strcmp(reason, "watchdog") == 0 || strcmp(reason, "brownout") == 0;
    db_log_event(TAG, crashed ? DB_EV_CRASHED : DB_EV_STARTED, reason);
    init_nvs();
    db_log_keep_start();
    started("settings", db_config_load());
    ESP_LOGI(TAG, "Deck Connector %s, controller %s", esp_app_get_description()->version,
             db_controller_name(db_config()->controller));

    started("deck screen", db_ui_start());
    db_net_on_change(on_net);
    started("deck", db_deck_start(strcmp(db_config()->usb_speed, "full") == 0, db_config()->max_decks));
    started("network", db_net_start());
    db_matter_start();
    db_ui_set_c6_version(db_ota_c6_version());
    db_ota_set_progress_cb(on_ota_progress);

    db_link_hooks_t link = {.status_json = link_status,
                            .c6_version = db_ota_c6_version,
                            .ota_requested = on_ota_requested,
                            .active_changed = db_ui_set_active_link};
    started("servers", db_link_start(&link));
    started("dock", db_dock_start(db_ui_hosts_changed));
    started("Satellite", db_satellite_start(on_satellite));
    db_link_set_enabled(db_config()->controller == DB_CTRL_SERVER);
    satellite_now(applied_sat); /* what the Satellite session reads at its start */
    db_config_on_save(apply_controller);
    started("web", db_web_start(web_status, web_state));
    started("Improv", db_serial_start());
    started("speaker", db_audio_start());
    esp_timer_handle_t sounds;
    esp_timer_create_args_t every = {.callback = sound_watch, .name = "sounds"};
    if (db_board()->speaker.present && esp_timer_create(&every, &sounds) == ESP_OK) {
        esp_timer_start_periodic(sounds, 1000000);
    }
    db_ota_confirm_when(healthy);
    xTaskCreate(setup_ap_task, "setup_ap", 3072, NULL, 2, NULL);
}
