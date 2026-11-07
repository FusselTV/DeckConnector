#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_json.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_satellite.h"
#include "db_ui.h"
#include "db_web.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define SETUP_AP_AFTER_S 60

static const char *TAG = "main";

static void write_device_fields(db_json_t *j, const db_device_info_t *dev)
{
    db_json_str(j, "model", dev->model);
    db_json_str(j, "serial", dev->serial);
    db_json_str(j, "firmware", dev->firmware);
    db_json_int(j, "keys", dev->key_count);
    db_json_int(j, "encoders", dev->encoder_count);
    db_json_int(j, "width", dev->displays[0].width);
    db_json_int(j, "height", dev->displays[0].height);
}

/* `decks` lists every attached deck (one for now; `?deck=<serial>` picks one); `device` is the first. */
static void write_device(db_json_t *j)
{
    /* too big for the callers' stacks, and not static: the web server, the web socket and the links call this
     * from their own tasks */
    db_device_info_t *dev = malloc(sizeof(*dev));
    if (!dev) {
        ESP_LOGE(TAG, "no memory for the device info");
        return; /* no "device" at all: null means no deck */
    }
    bool present = db_deck_info(dev);
    db_json_arr_open(j, "decks");
    if (present) {
        db_json_obj_open(j, NULL);
        db_json_int(j, "number", 1);
        write_device_fields(j, dev);
        db_json_str(j, "usb_speed", db_deck_full_speed() ? "full" : "high");
        db_json_bool(j, "hub", db_deck_behind_hub());
        db_json_obj_close(j);
    }
    db_json_arr_close(j);
    if (present) {
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
    db_json_str(j, "ssid", db_config()->wifi_ssid);
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
                         int source, const db_health_t *health)
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
        db_json_int(j, "since_s", health->since_us / 1000000); /* after the start: an age would change the live
                                                                state every second */
        db_json_str(j, "iface", connected ? (health->over_wlan ? "wlan" : "lan") : "");
    }
    db_json_bool(j, "chosen", source >= 0 && (db_source_t)source == db_ui_chosen_source());
    db_json_bool(j, "drawing", source >= 0 && (db_source_t)source == db_deck_writer());
    db_json_obj_close(j);
}

/* Every server that is set or was found, Companion's Satellite API when set, and every Network Dock host. */
static void write_sources(db_json_t *j, bool live)
{
    db_json_arr_open(j, "sources");
    for (int i = 0; i < DB_MAX_SERVERS; i++) {
        if (db_link_url(i)[0]) {
            db_health_t h = db_link_health(i);
            write_source(j, "server", db_link_url(i), 0, db_link_connected(i),
                         db_link_connected(i) ? DB_SOURCE_LINK0 + i : -1, &h);
        }
    }
    if (db_satellite_address()[0]) {
        db_health_t h = db_satellite_health();
        write_source(j, "companion", db_satellite_address(), db_satellite_peer(), db_satellite_connected(),
                     DB_SOURCE_SAT, &h);
    }
    db_dock_host_t hosts[DB_DOCK_HOSTS];
    size_t n = db_dock_hosts(hosts, DB_DOCK_HOSTS);
    for (size_t i = 0; i < n; i++) {
        const char *kind = hosts[i].kind == DB_CTRL_SERVER ? "unknown" : db_controller_name(hosts[i].kind);
        write_source(j, kind, hosts[i].ip, inet_addr(hosts[i].ip), hosts[i].source >= 0, hosts[i].source, NULL);
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
        db_json_int(j, "since_s", denied[i].at_us / 1000000); /* seconds after the start, like sources[].since_s */
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

/* Shared body of the server `status` message, /api/status and the web socket state. Without `live` it leaves
 * out what changes all the time (uptime, signal, counters), so a change in it is worth telling. */
static const char *boot_reason(void)
{
    static const char *const reasons[] = {
        [ESP_RST_POWERON] = "power",  [ESP_RST_EXT] = "pin",        [ESP_RST_SW] = "restart",
        [ESP_RST_PANIC] = "crash",    [ESP_RST_INT_WDT] = "watchdog", [ESP_RST_TASK_WDT] = "watchdog",
        [ESP_RST_WDT] = "watchdog",   [ESP_RST_BROWNOUT] = "brownout",
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

static void write_status(db_json_t *j, bool live)
{
    db_deck_stats_t st;
    db_deck_stats(&st);
    const char *c6 = db_ota_c6_version();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    char id[13];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    db_json_str(j, "id", id); /* stable, unlike the name: for Home Assistant's unique_id */
    db_json_str(j, "https_fingerprint", db_web_fingerprint());
    db_json_bool(j, "https_custom", db_web_cert_custom());
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
    db_json_str(j, "writer", db_deck_source_name(db_deck_writer()));
    db_json_bool(j, "menu_open", db_ui_menu_open());
    write_device(j);
    write_network(j, live);
    write_sources(j, live);
    if (!live) {
        return;
    }
    db_json_obj_open(j, "counters");
    db_json_int(j, "images", st.images);
    db_json_int(j, "superseded", st.superseded);
    db_json_int(j, "rejected", st.rejected);
    db_json_int(j, "usb_errors", st.usb_errors);
    db_json_str(j, "usb_speed", db_deck_full_speed() ? "full" : "high");
    db_json_int(j, "inputs", st.inputs);
    db_json_int(j, "inputs_dropped", st.inputs_dropped);
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
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static size_t web_state(char *buf, size_t cap)
{
    return tagged_status(buf, cap, "state", false);
}

static void on_ota_progress(const char *target, const char *state, int pct, const char *error)
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
    }
    db_json_obj_close(&j);
    if (db_json_end(&j)) {
        db_link_broadcast(buf);
        db_web_broadcast(buf);
    }
    bool running = strcmp(state, "error") != 0;
    db_ui_set_update(running ? pct : -1, target[0] == 'c' ? "WLAN-Chip" : "Firmware");
}

static void on_ota_requested(const char *target, const char *url)
{
    if (db_ota_start_url(url) != ESP_OK) {
        on_ota_progress(target, "error", -1, "busy");
    }
}

static void apply_controller(void)
{
    db_link_reload();
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
    db_ui_refresh(); /* also for setup_done and qr_login, which change what the deck shows */
}

/* A new bridge without any network opens the setup WLAN by itself. It closes again once the network is there and
 * no phone is on it: the wizard joins the WLAN over it and still has to save. */
static void setup_ap_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(SETUP_AP_AFTER_S * 1000));
    if (!db_config()->setup_done && !db_net_online() && db_net_ap_start() == ESP_OK) {
        ESP_LOGW(TAG, "no network after %d s, opening the setup WLAN", SETUP_AP_AFTER_S);
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
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
#ifdef DB_TEST_CRASH
    abort();
#endif
    db_log_start();
    db_log_event(TAG, "boot", "started (%s)", boot_reason());
    init_nvs();
    ESP_ERROR_CHECK(db_config_load());
    ESP_LOGI(TAG, "DeckConnector %s, controller %s", esp_app_get_description()->version,
             db_controller_name(db_config()->controller));

    ESP_ERROR_CHECK(db_ui_start());
    ESP_ERROR_CHECK(db_deck_start(strcmp(db_config()->usb_speed, "full") == 0));
    ESP_ERROR_CHECK(db_net_start());
    db_ui_set_c6_version(db_ota_c6_version());
    db_ota_set_progress_cb(on_ota_progress);

    db_link_hooks_t link = {.status_json = link_status,
                            .c6_version = db_ota_c6_version,
                            .ota_requested = on_ota_requested,
                            .active_changed = db_ui_set_active_link};
    ESP_ERROR_CHECK(db_link_start(&link));
    ESP_ERROR_CHECK(db_dock_start(db_ui_hosts_changed));
    ESP_ERROR_CHECK(db_satellite_start(db_ui_hosts_changed));
    db_link_set_enabled(db_config()->controller == DB_CTRL_SERVER);
    db_config_on_save(apply_controller);
    ESP_ERROR_CHECK(db_web_start(web_status, web_state));
    db_ota_confirm_when(db_net_online);
    xTaskCreate(setup_ap_task, "setup_ap", 3072, NULL, 2, NULL);
}
