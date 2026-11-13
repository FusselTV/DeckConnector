#include "db_ui.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_dns.h"
#include "db_dock.h"
#include "db_json.h"
#include "db_link.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_satellite.h"
#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_port.h"
#include "lwip/ip4_addr.h"
#include "qrcode.h"
#include "ui.h"

#define QUALITY       85
#define REDRAW_DELAY  10      /* ms: the events of one turn or press come in a burst */
#define FULL_ABOVE    5       /* more changed areas than this go out as one full image, measured faster */
#define SAVE_AFTER_US 1500000 /* brightness goes to flash once it rests */
#define PASSWORD_US   30000000
#define RESET_ASK_US  60000000
#define AGAIN_US      3000000 /* a second call within this changes nothing */
#define HANDOVER_US   5000000

static const char *TAG = "ui";

static struct {
    TaskHandle_t task;
    SemaphoreHandle_t lock;    /* menu, between the deck's event task and the UI task */
    SemaphoreHandle_t qr_lock; /* the QR buffers, which model() fills on both tasks */
    int active_link;
    int update_pct;
    bool update_wlan;
    char c6[16];
    db_device_info_t dev;
    bool present;
    ui_canvas_t canvas;
    uint32_t seq;
    bool enc_down[8];
    bool key_down[UI_MAX_KEYS];
    int64_t chord_since;
    bool covered;
    bool full;         /* the next picture goes out whole: someone else drew meanwhile */
    ui_screen_t shown; /* what is on the deck, to send only what changed */
    ui_menu_t menu;
    volatile int brightness; /* applied but not saved yet, -1 when saved */
    int lit;       /* what the device UI last set on the deck; -1: someone else may have changed it */
    int before;    /* the deck's brightness when the menu opened; -1: none */
    bool turned;   /* the dial moved in this menu: from then on the saved brightness counts */
    bool menu_lit; /* the UI lit the deck for the menu */
    esp_timer_handle_t save_timer;
    bool save_due;
    int64_t
        handover_until; /* Companion moves to a new Satellite address: the deck keeps its picture till then */
} s = {.active_link = -1,
       .update_pct = -1,
       .full = true,
       .menu = {.confirm_key = -1},
       .brightness = -1,
       .lit = -1,
       .before = -1};

static void octets_of(const char *ip, uint8_t out[4])
{
    ip4_addr_t a = {0};
    ip4addr_aton(ip, &a);
    for (int o = 0; o < 4; o++) {
        out[o] = (uint8_t)(ip4_addr_get_u32(&a) >> (8 * o));
    }
}

/* The dock host of `kind` that draws when `kind` is picked: the configured one if it is there, else the
 * first. */
static int dock_choice(db_controller_t kind, const db_dock_host_t *hosts, size_t n)
{
    int pick = -1;
    for (size_t i = 0; i < n; i++) {
        if (hosts[i].kind != kind || hosts[i].source < 0) {
            continue;
        }
        pick = pick < 0 ? (int)i : pick;
        if (strcmp(hosts[i].ip, db_config()->host) == 0) {
            return (int)i;
        }
    }
    return pick;
}

/* Companion over its Satellite API draws unless the config names a dock host of it that is there. */
static db_source_t choice(db_controller_t kind)
{
    if (kind == DB_CTRL_SERVER) {
        return s.active_link >= 0 ? (db_source_t)(DB_SOURCE_LINK0 + s.active_link) : DB_SOURCE_UI;
    }
    db_dock_host_t hosts[DB_DOCK_HOSTS];
    size_t n = db_dock_hosts(hosts, DB_DOCK_HOSTS);
    int i = dock_choice(kind, hosts, n);
    bool named = i >= 0 && strcmp(hosts[i].ip, db_config()->host) == 0;
    if (kind == DB_CTRL_COMPANION && db_satellite_connected() && !named) {
        return DB_SOURCE_SAT;
    }
    return i >= 0 ? (db_source_t)hosts[i].source : DB_SOURCE_UI;
}

db_source_t db_ui_chosen_source(void)
{
    return choice(db_config()->controller);
}

/* "ws://192.168.1.5:8765/x" -> "192.168.1.5:8765" */
static void url_label(const char *url, char *out, size_t cap)
{
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t len = strcspn(h, "/");
    snprintf(out, cap, "%.*s", (int)len, h);
}

/* The name the router knows for `ip` without its domain, else the address. */
static void host_label(uint32_t ip, const char *addr, char *out, size_t cap)
{
    char name[40];
    db_dock_name(ip, name);
    name[strcspn(name, ".")] = '\0';
    snprintf(out, cap, "%s", name[0] ? name : addr);
}

static ui_source_t *add_source(ui_model_t *md, uint8_t kind, uint8_t via, db_source_t source)
{
    ui_source_t *src = &md->sources[md->source_count++];
    db_controller_t chosen = db_config()->controller;
    *src = (ui_source_t){.kind = kind, .via = via, .chosen = kind == chosen && choice(chosen) == source};
    md->hosts[kind]++;
    return src;
}

static void model_sources(ui_model_t *md)
{
    if (db_satellite_connected()) {
        ui_source_t *src = add_source(md, UI_CTRL_COMPANION, UI_VIA_SATELLITE, DB_SOURCE_SAT);
        snprintf(src->id, sizeof(src->id), "satellite");
        host_label(db_satellite_peer(), db_satellite_address(), src->label, sizeof(src->label));
    }
    db_dock_host_t hosts[DB_DOCK_HOSTS];
    size_t n = db_dock_hosts(hosts, DB_DOCK_HOSTS);
    for (size_t i = 0; i < n && md->source_count < UI_SOURCES; i++) {
        if (hosts[i].source < 0 || hosts[i].kind == DB_CTRL_SERVER) {
            continue; /* no device port yet, or not told what it is */
        }
        ui_source_t *src = add_source(md, (uint8_t)hosts[i].kind, UI_VIA_DOCK, (db_source_t)hosts[i].source);
        snprintf(src->id, sizeof(src->id), "%s", hosts[i].ip);
        host_label(ipaddr_addr(hosts[i].ip), hosts[i].ip, src->label, sizeof(src->label));
    }
    for (int i = 0; i < DB_MAX_SERVERS && md->source_count < UI_SOURCES; i++) {
        if (db_link_connected(i)) {
            ui_source_t *src =
                add_source(md, UI_CTRL_SERVER, UI_VIA_SERVER, (db_source_t)(DB_SOURCE_LINK0 + i));
            snprintf(src->id, sizeof(src->id), "%s", db_link_url(i));
            url_label(src->id, src->label, sizeof(src->label));
        }
    }
}

/* WIFI:T:WPA;S:name;P:password;; with \ before \ ; , : and ", the format phone cameras read. */
static void wifi_qr_text(char *out, size_t cap, const char *ssid, const char *pass)
{
    size_t n = 0;
    const char *parts[] = {"WIFI:T:WPA;S:", ssid, ";P:", pass, ";;"};
    for (int i = 0; i < 5; i++) {
        for (const char *c = parts[i]; *c && n + 2 < cap; c++) {
            if (i % 2 && strchr("\\;,:\"", *c)) {
                out[n++] = '\\';
            }
            out[n++] = *c;
        }
    }
    out[n] = '\0';
}

static void changed(void);

static ui_qr_t *qr_target;

static void take_qr(esp_qrcode_handle_t code)
{
    int size = esp_qrcode_get_size(code);
    if (size > UI_QR_MAX) {
        return;
    }
    memset(qr_target, 0, sizeof(*qr_target));
    qr_target->size = (uint8_t)size;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (esp_qrcode_get_module(code, x, y)) {
                qr_target->bits[(y * size + x) / 8] |= (uint8_t)(1 << ((y * size + x) % 8));
            }
        }
    }
}

static bool make_qr(ui_qr_t *qr, const char *text)
{
    qr_target = qr;
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = take_qr;
    cfg.max_qrcode_version = 6;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    qr->size = 0;
    esp_log_level_set("QRCODE", ESP_LOG_WARN); /* it logs the text it encodes: a password or a login */
    return esp_qrcode_generate(&cfg, text) == ESP_OK;
}

/* Made again only when the setup WLAN's name or password changed. */
static void model_qr(ui_model_t *md)
{
    static char made_for[128];
    static ui_qr_t qr;
    char text[sizeof(made_for)];
    wifi_qr_text(text, sizeof(text), md->ap_ssid, md->ap_pass);
    if (md->ap_on && strcmp(text, made_for) != 0 && make_qr(&qr, text)) {
        strcpy(made_for, text);
    }
    md->ap_qr = md->ap_on ? qr : (ui_qr_t){0};
}

#define LOGIN_US (10 * 60 * 1000000LL)
static portMUX_TYPE login_mux = portMUX_INITIALIZER_UNLOCKED;
static char login_token[17];
static int64_t login_made;
static uint8_t setup_step;
static int64_t pass_until, pass_asked, reset_until, reset_asked; /* under login_mux */
static struct { /* the login request on the deck, under login_mux */
    int64_t until;
    uint32_t seq;
    uint32_t ip;
    char ip_s[16];
    char who[97];
    char mac[18];
    int answer; /* -1 while none */
} ask = {.answer = -1};

static int64_t until(const int64_t *t)
{
    portENTER_CRITICAL(&login_mux);
    int64_t v = *t;
    portEXIT_CRITICAL(&login_mux);
    return v;
}

static bool overlay(int64_t now)
{
    return now < until(&pass_until) || now < until(&reset_until) || now < until(&ask.until);
}

/* A code made before setup was done must not outlive it: the model that clears it is only built while the
 * deck shows its own screen, not while a host draws. */
static bool login_wanted(void)
{
    const db_config_t *c = db_config();
    return !c->setup_done && !c->qr_login_off;
}

bool db_ui_login_take(const char *token)
{
    bool wanted = login_wanted();
    portENTER_CRITICAL(&login_mux);
    uint8_t diff =
        !wanted || strlen(token) != 16 || !login_token[0] || esp_timer_get_time() - login_made >= LOGIN_US;
    for (int i = 0; i < 16 && token[i]; i++) {
        diff |= (uint8_t)(token[i] ^ login_token[i]);
    }
    if (!diff) {
        login_token[0] = '\0';
    }
    portEXIT_CRITICAL(&login_mux);
    if (!diff) {
        changed(); /* the next code */
    }
    return !diff;
}

bool db_ui_login_shown(void)
{
    bool wanted = login_wanted();
    int64_t now = esp_timer_get_time();
    db_net_status_t n;
    db_net_status(&n);
    portENTER_CRITICAL(&login_mux);
    bool shown = (wanted && login_token[0] != '\0') || now < pass_until || n.ap_active;
    portEXIT_CRITICAL(&login_mux);
    return shown;
}

bool db_ui_password_shown(void)
{
    /* the menu and the setup pages show it too, and a control token may open the menu */
    return db_ui_login_shown() || !db_config()->setup_done || db_ui_menu_open();
}

void db_ui_setup_step(int step)
{
    setup_step = (uint8_t)step;
    changed();
}

/* While setup is not done: http://<ip>/?t=<token>, a new token after each use and every 10 min. */
static void model_login(ui_model_t *md, const db_config_t *c)
{
    const char *ip = md->lan_up ? md->lan_ip : (md->wlan_up ? md->wlan_ip : "");
    bool want = login_wanted() && ip[0];
    char fresh[17];
    uint8_t raw[8];
    esp_fill_random(raw, sizeof(raw));
    db_hex(fresh, raw, sizeof(raw));
    portENTER_CRITICAL(&login_mux);
    if (!want) {
        login_token[0] = '\0';
    } else if (!login_token[0] || md->now_us - login_made >= LOGIN_US) {
        strcpy(login_token, fresh);
        login_made = md->now_us;
    }
    strcpy(md->login_token, login_token);
    portEXIT_CRITICAL(&login_mux);
    static char made_for[64];
    static ui_qr_t qr;
    char url[sizeof(made_for)];
    snprintf(url, sizeof(url), "%s://%s/?t=%s", c->https_only ? "https" : "http", ip, md->login_token);
    if (want && strcmp(url, made_for) != 0 && make_qr(&qr, url)) {
        strcpy(made_for, url);
    }
    md->login_qr = want ? qr : (ui_qr_t){0};
}

/* A Companion that announces its Satellite API while no address is set and nobody said no. */
static void model_offer(ui_model_t *md, const db_config_t *c)
{
    db_net_found_t found;
    if (c->satellite[0] || c->satellite_auto_off || !db_net_companions(&found, 1)) {
        return;
    }
    /* "Companion (studio)" -> "studio": the key already says Companion */
    const char *name = found.name[0] ? found.name : found.host;
    size_t len = strlen(name);
    bool wrapped = strncmp(name, "Companion (", 11) == 0 && len > 12 && name[len - 1] == ')';
    snprintf(md->offer_name, sizeof(md->offer_name), "%.*s", wrapped ? (int)len - 12 : (int)len,
             wrapped ? name + 11 : name);
    snprintf(md->offer_addr, sizeof(md->offer_addr), "%s:%u", found.host, found.port);
}

static void model(ui_model_t *md)
{
    const db_config_t *c = db_config();
    db_net_status_t n;
    db_net_status(&n);
    *md = (ui_model_t){
        .now_us = esp_timer_get_time(),
        .controller = c->controller,
        .lan_link = n.lan_link,
        .lan_up = n.lan_up,
        .lan_dhcp = c->lan.dhcp,
        .wlan_chip = n.wlan_chip,
        .wlan_up = n.wlan_up,
        .wlan_rssi = n.rssi,
        .ap_on = n.ap_active,
        .brightness = s.brightness >= 0 ? s.brightness : c->brightness,
        .update_pct = s.update_pct,
        .setup_done = c->setup_done,
        .setup_step = setup_step,
        .picked = c->host[0] || c->controller != DB_CTRL_COMPANION,
        .menu_hold_ms = c->menu_hold_ms,
        .english = strcmp(c->lang, "en") == 0,
        .sat_failed = db_satellite_health().error[0] != '\0',
    };
    if (ui_menu_open(&s.menu) && !s.turned && s.before >= 0) {
        md->brightness = s.before; /* the deck shows at least 20 %, the number stays what it had */
    }
    md->show_pass = md->now_us < until(&pass_until);
    md->reset_ask = md->now_us < until(&reset_until);
    uint32_t ask_ip = 0;
    portENTER_CRITICAL(&login_mux);
    if (md->now_us < ask.until) {
        ask_ip = ask.ip;
        md->login_seq = ask.seq;
        strcpy(md->login_ip, ask.ip_s);
        /* 20 characters fit two lines on every key; never cut inside a UTF-8 character */
        size_t n = 0;
        for (int chars = 0; ask.who[n] && n < sizeof(md->login_who) - 4; n++) {
            if ((ask.who[n] & 0xc0) != 0x80 && ++chars > 20) {
                break;
            }
        }
        while (n && (ask.who[n] & 0xc0) == 0x80) {
            n--;
        }
        memcpy(md->login_who, ask.who, n);
        strcpy(md->login_mac, ask.mac);
    }
    portEXIT_CRITICAL(&login_mux);
    if (ask_ip) {
        db_dock_name(ask_ip, md->login_name);
    }
    snprintf(md->menu_chord, sizeof(md->menu_chord), "%s", c->menu_chord);
    model_offer(md, c);
    snprintf(md->lan_ip, sizeof(md->lan_ip), "%s", n.lan_ip);
    snprintf(md->wlan_ip, sizeof(md->wlan_ip), "%s", n.wlan_ip);
    snprintf(md->wifi_ssid, sizeof(md->wifi_ssid), "%s", c->wifi_ssid);
    snprintf(md->ap_ssid, sizeof(md->ap_ssid), "%s", n.ap_ssid);
    snprintf(md->ap_pass, sizeof(md->ap_pass), "%s", n.ap_pass);
    snprintf(md->ap_ip, sizeof(md->ap_ip), "%s", n.ap_ip);
    xSemaphoreTake(s.qr_lock, portMAX_DELAY);
    model_qr(md);
    model_login(md, c);
    xSemaphoreGive(s.qr_lock);
    if (db_dock_request(md->request)) {
        db_dock_name(ipaddr_addr(md->request), md->request_name);
    }
    snprintf(md->server_url, sizeof(md->server_url), "%s", db_config_server_of(c, 0));
    if (!c->host[0] || strcmp(c->host, "satellite") == 0) { /* else Companion is a dock host */
        snprintf(md->satellite, sizeof(md->satellite), "%s", c->satellite);
    }
    model_sources(md);
    snprintf(md->web_pass, sizeof(md->web_pass), "%s", c->admin_pass);
    snprintf(md->fw, sizeof(md->fw), "%s", esp_app_get_description()->version);
    snprintf(md->wlan_fw, sizeof(md->wlan_fw), "%s", s.c6);
    md->update_wlan = s.update_wlan;
    db_ip_config_t lan = c->lan;
    if (lan.ip[0] == '\0') {
        snprintf(lan.ip, sizeof(lan.ip), "%s", n.lan_ip); /* start from the address DHCP gave */
    }
    if (lan.mask[0] == '\0') {
        snprintf(lan.mask, sizeof(lan.mask), "255.255.255.0");
    }
    const char *fields[] = {lan.ip, lan.mask, lan.gateway, lan.dns};
    for (int f = 0; f < 4; f++) {
        octets_of(fields[f], md->lan_cfg[f]);
    }
}

static db_source_t wanted_writer(void)
{
    char request[16];
    if (ui_menu_open(&s.menu) || s.update_pct >= 0 || !db_config()->setup_done || db_dock_request(request) ||
        s.menu.page == PAGE_WELCOME_NET || overlay(esp_timer_get_time())) {
        return DB_SOURCE_UI;
    }
    const db_config_t *c = db_config();
    static char satellite[sizeof(c->satellite)];
    if (strcmp(satellite, c->satellite) != 0) {
        snprintf(satellite, sizeof(satellite), "%s", c->satellite);
        if (c->satellite[0] && c->controller == DB_CTRL_COMPANION && db_deck_writer() != DB_SOURCE_UI) {
            s.handover_until = esp_timer_get_time() + HANDOVER_US;
        }
    }
    db_source_t chosen = db_ui_chosen_source();
    if (chosen == DB_SOURCE_SAT) {
        s.handover_until = 0;
    }
    /* nothing draws as the Satellite client while it connects, so the old picture stays */
    return chosen == DB_SOURCE_UI && esp_timer_get_time() < s.handover_until ? DB_SOURCE_SAT : chosen;
}

static void pick_writer(void)
{
    /* The menu or an update drew over the active server's picture; tell it to draw again once they end. */
    bool covering = (ui_menu_open(&s.menu) || s.update_pct >= 0) && s.active_link >= 0;
    db_source_t wanted = wanted_writer();
    if (wanted == DB_SOURCE_UI && db_deck_writer() != DB_SOURCE_UI) {
        s.full = true;
    }
    db_source_t was = db_deck_writer();
    if (wanted != DB_SOURCE_UI && was == DB_SOURCE_UI && s.menu_lit) {
        int saved = s.brightness >= 0 ? s.brightness : db_config()->brightness;
        int back = ui_after_menu(s.lit, s.before, s.turned, saved);
        if (back >= 0) {
            db_deck_set_brightness((uint8_t)back); /* a host that sends its own sets it again after this */
        }
        s.menu_lit = false;
    }
    db_deck_set_writer(wanted, esp_timer_get_time() < s.handover_until);
    if (wanted != was && wanted >= DB_SOURCE_LINK0 && wanted < DB_SOURCE_DOCK0) {
        db_link_brightness_again(); /* a server chosen while another drew: its own brightness, not the last */
    }
    if (s.covered && !covering && s.active_link >= 0) {
        db_link_resend_role();
    }
    s.covered = covering;
}

/* The UI task picks the writer: then the brightness it sets for the menu and the one it gives back after it
 * never cross. */
static void changed(void)
{
    if (s.task) {
        xTaskNotifyGive(s.task);
    } else { /* db_ui_start failed: the bridge runs on without the deck screens */
        pick_writer();
    }
}

/* Under the edit lock, like a change from the web, so neither overwrites a save made in between. */
static void edit_begin(db_config_t *c)
{
    db_config_edit_begin();
    db_config_copy(c);
}

static esp_err_t edit_end(const db_config_t *c, const char *what)
{
    esp_err_t err = db_config_save(c);
    db_config_edit_end();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s not saved: %s", what, esp_err_to_name(err));
    }
    return err;
}

static void note(const char *de, const char *en, bool to_main)
{
    const char *text = strcmp(db_config()->lang, "en") == 0 ? en : de;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (to_main) {
        ui_menu_open_main(&s.menu, esp_timer_get_time());
    }
    snprintf(s.menu.note, sizeof(s.menu.note), "%s", text);
    xSemaphoreGive(s.lock);
}

static void save_brightness(void)
{
    int b = s.brightness;
    if (b < 0) {
        return;
    }
    db_config_t c;
    edit_begin(&c);
    c.brightness = (uint8_t)b;
    if (edit_end(&c, "brightness") == ESP_OK &&
        s.brightness == b) { /* a turn during the save keeps its value */
        s.brightness = -1;
    }
}

static void save_lan(const ui_action_t *act)
{
    db_config_t c;
    edit_begin(&c);
    c.lan.dhcp = act->dhcp;
    char *fields[] = {c.lan.ip, c.lan.mask, c.lan.gateway, c.lan.dns};
    for (int f = 0; f < 4; f++) {
        const uint8_t *o = act->octets[f];
        snprintf(fields[f], 16, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
    }
    if (edit_end(&c, "LAN") == ESP_OK) {
        db_ota_restart(800);
    } else {
        note("Nicht gespeichert", "Not saved", false);
    }
}

static void carry_out(const ui_action_t *act, const ui_model_t *md)
{
    db_config_t c;
    switch (act->what) {
    case UI_DO_ANSWER:
        db_dock_answer(act->value);
        break;
    case UI_DO_SOURCE:
        edit_begin(&c);
        c.controller = (db_controller_t)md->sources[act->value].kind;
        snprintf(c.host, sizeof(c.host), "%s", md->sources[act->value].id);
        c.setup_done = true;
        edit_end(&c, "host choice");
        break;
    case UI_DO_OFFER:
        edit_begin(&c);
        if (!act->value) {
            c.satellite_auto_off = true;
            edit_end(&c, "no to Satellite");
            break;
        }
        c.satellite_auto_off = false;
        snprintf(c.satellite, sizeof(c.satellite), "%s", md->offer_addr);
        c.controller = DB_CTRL_COMPANION;
        snprintf(c.host, sizeof(c.host), "satellite");
        c.setup_done = true;
        ESP_LOGI(TAG, "Companion %s over Satellite, chosen on the deck", md->offer_addr);
        edit_end(&c, "Companion address");
        break;
    case UI_DO_BRIGHTNESS: {
        int b = md->brightness + act->value;
        s.brightness = b < 0 ? 0 : (b > 100 ? 100 : b); /* the UI task puts it on the deck */
        s.turned = true;
        esp_timer_stop(s.save_timer);
        esp_timer_start_once(s.save_timer, SAVE_AFTER_US);
        break;
    }
    case UI_DO_AP_START:
        db_net_ap_start();
        break;
    case UI_DO_AP_STOP:
        db_net_ap_stop();
        break;
    case UI_DO_RESET:
        ESP_LOGW(TAG, "factory reset from the deck");
        if (db_ui_factory_reset() == ESP_OK) {
            db_ota_restart(800);
            break;
        }
        ESP_LOGE(TAG, "factory reset failed, no restart");
        portENTER_CRITICAL(&login_mux);
        reset_until = 0;
        portEXIT_CRITICAL(&login_mux);
        note("Löschen fehlgeschlagen", "Reset failed", true);
        break;
    case UI_DO_RESTART:
        db_ota_restart(800);
        break;
    case UI_DO_SAVE_LAN:
        save_lan(act);
        break;
    case UI_DO_HIDE_PASSWORD:
        portENTER_CRITICAL(&login_mux);
        pass_until = 0;
        portEXIT_CRITICAL(&login_mux);
        break;
    case UI_DO_CANCEL_RESET:
        portENTER_CRITICAL(&login_mux);
        reset_until = 0;
        portEXIT_CRITICAL(&login_mux);
        break;
    case UI_DO_LOGIN:
        portENTER_CRITICAL(&login_mux);
        bool same = ask.seq == md->login_seq && ask.answer < 0;
        if (same) {
            ask.answer = act->value;
            ask.until = 0;
        }
        portEXIT_CRITICAL(&login_mux);
        if (same) {
            ESP_LOGI(TAG, "login request from %s %s on the deck", md->login_ip,
                     act->value ? "accepted" : "denied");
        }
        break;
    case UI_DO_SETUP_DONE:
        edit_begin(&c);
        c.setup_done = true;
        edit_end(&c, "setup done");
        break;
    case UI_DO_LANG:
        edit_begin(&c);
        snprintf(c.lang, sizeof(c.lang), "%s", md->english ? "de" : "en");
        edit_end(&c, "language");
        break;
    case UI_DO_SHOW_PASSWORD:
        db_ui_show_password();
        break;
    default:
        break;
    }
}

static bool ensure_canvas(const db_display_t *d)
{
    if (s.canvas.px && s.canvas.w == d->width && s.canvas.h == d->height) {
        return true;
    }
    heap_caps_free(s.canvas.px);
    s.canvas = (ui_canvas_t){.w = d->width, .h = d->height, .px = jpeg_port_alloc_rgb(d->width, d->height)};
    s.full = true;
    if (!s.canvas.px) {
        return false;
    }
    memset(s.canvas.px, 0, (size_t)d->width * d->height * 3); /* the gaps between keys, never drawn again */
    return true;
}

/* Copy one rectangle out of the canvas, encode it and queue it. */
static bool submit_rect(db_rect_t r)
{
    static uint8_t *area; /* reused: one key or a strip part at a time */
    static size_t area_px;
    const uint8_t *src = s.canvas.px;
    if (r.w != s.canvas.w || r.h != s.canvas.h) {
        if ((size_t)r.w * r.h > area_px) {
            heap_caps_free(area);
            area = jpeg_port_alloc_rgb(r.w, r.h);
            area_px = area ? (size_t)r.w * r.h : 0;
            if (!area) {
                return false;
            }
        }
        for (int y = 0; y < r.h; y++) {
            memcpy(area + (size_t)y * r.w * 3, s.canvas.px + ((size_t)(r.y + y) * s.canvas.w + r.x) * 3,
                   (size_t)r.w * 3);
        }
        src = area;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = jpeg_port_encode_for(&s.dev.displays[0], src, r.w, r.h, QUALITY, &jpeg, &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "encode %ux%u failed: %s", r.w, r.h, esp_err_to_name(err));
        return false;
    }
    db_deck_submit(DB_SOURCE_UI, 0, r, ++s.seq, jpeg, len, (uint64_t)esp_timer_get_time());
    return true;
}

static void render(const ui_screen_t *sc)
{
    const db_display_t *d = &s.dev.displays[0];
    if (!ensure_canvas(d)) {
        return;
    }
    const db_target_t *full = NULL;
    for (int i = 0; i < d->target_count; i++) {
        full = d->targets[i].kind == DB_TARGET_FULL ? &d->targets[i] : full;
    }
    bool whole = s.full;
    s.full = false; /* a writer change while drawing sets it again */
    db_rect_t dirty[DB_MAX_TARGETS + UI_SEGMENTS];
    int n = ui_render(&s.canvas, &s.dev, sc, whole ? NULL : &s.shown, dirty, DB_MAX_TARGETS + UI_SEGMENTS);
    s.shown = *sc;
    bool sent = true;
    if (full && n && (whole || n > FULL_ABOVE)) {
        sent = submit_rect(full->rect);
    } else {
        for (int i = 0; i < n; i++) {
            sent &= submit_rect(dirty[i]);
        }
    }
    if (!sent) {
        s.full = true; /* what the deck shows is unknown now */
    }
}

/* The save writes flash and stores a 1.4 KB copy of the settings: not on the esp_timer task. */
static void brightness_due(void *arg)
{
    s.save_due = true;
    if (s.task) {
        xTaskNotifyGive(s.task);
    }
}

static void ui_task(void *arg)
{
    static ui_screen_t sc; /* 5 KB, kept off the task stack */
    static ui_model_t md;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(REDRAW_DELAY));
        ulTaskNotifyTake(pdTRUE, 0);
        if (__atomic_exchange_n(&s.save_due, false, __ATOMIC_RELAXED)) {
            save_brightness();
        }
        pick_writer();
        if (!s.present || db_deck_writer() != DB_SOURCE_UI) {
            s.lit = -1; /* a host sets its own; when the UI draws again it sets its own once more */
            s.menu_lit = false;
            continue;
        }
        xSemaphoreTake(s.lock, portMAX_DELAY);
        bool menu = ui_menu_open(&s.menu);
        xSemaphoreGive(s.lock);
        if (menu && !s.menu_lit) { /* just opened: what the deck has now, before the floor */
            s.before = db_deck_brightness();
            s.turned = false;
        }
        model(&md);
        int light = ui_light(menu, md.brightness);
        if (light != s.lit) {
            s.lit = light;
            db_deck_set_brightness((uint8_t)light);
        }
        s.menu_lit = menu;
        ui_action_t first;
        if (ui_first_source(&md, &first)) {
            ESP_LOGI(TAG, "first host draws: %s", md.sources[0].id);
            carry_out(&first, &md);
            continue; /* the save brings the next round, with that host as the writer */
        }
        ui_layout_t l = ui_layout_of(&s.dev);
        if (s.update_pct >= 0) {
            ui_update_screen(&md, &l, &sc);
        } else {
            xSemaphoreTake(s.lock, portMAX_DELAY);
            ui_menu_screen(&s.menu, &md, &l, &sc);
            xSemaphoreGive(s.lock);
        }
        render(&sc);
    }
}

bool db_ui_menu_chord(int encoders, int keys, bool *on_encoders, int *last)
{
    return ui_menu_chord(db_config()->menu_chord, encoders, keys, on_encoders, last);
}

static void open_menu(int64_t now)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool opened = !ui_menu_open(&s.menu);
    if (opened) {
        ui_menu_open_main(&s.menu, now);
    }
    xSemaphoreGive(s.lock);
    if (opened) {
        ESP_LOGI(TAG, "menu opened");
        changed();
    }
}

esp_err_t db_ui_open_menu(void)
{
    if (!s.present) {
        return ESP_ERR_INVALID_STATE;
    }
    open_menu(esp_timer_get_time());
    return ESP_OK;
}

void db_ui_close_menu(void)
{
    int64_t now = esp_timer_get_time();
    bool covered = overlay(now);
    portENTER_CRITICAL(&login_mux);
    pass_until = 0;
    reset_until = 0;
    if (now < ask.until) {
        ask.answer = 0;
        ask.until = 0;
    }
    portEXIT_CRITICAL(&login_mux);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool open = ui_menu_open(&s.menu);
    if (open) {
        ui_menu_close(&s.menu, now);
    }
    xSemaphoreGive(s.lock);
    if (open || covered) {
        ESP_LOGI(TAG, "menu closed");
        changed();
    }
}

bool db_ui_menu_open(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool open = ui_menu_open(&s.menu);
    xSemaphoreGive(s.lock);
    return open || overlay(esp_timer_get_time());
}

static void check_menu_chord(void)
{
    int last;
    bool on_encoders;
    bool any = db_ui_menu_chord(s.dev.encoder_count, db_device_image_keys(&s.dev), &on_encoders, &last);
    const bool *down = on_encoders ? s.enc_down : s.key_down;
    int size = on_encoders ? sizeof(s.enc_down) : sizeof(s.key_down);
    bool held = any && last < size && down[0] && down[last];
    int64_t now = esp_timer_get_time();
    if (!held) {
        s.chord_since = 0;
    } else if (!s.chord_since) {
        s.chord_since = now;
    } else if (now - s.chord_since >= (int64_t)db_config()->menu_hold_ms * 1000) {
        s.chord_since = 0;
        open_menu(now);
    }
}

static void on_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_ENCODER_DOWN || ev->kind == DB_INPUT_ENCODER_UP) {
        s.enc_down[ev->index & 7] = ev->kind == DB_INPUT_ENCODER_DOWN;
    } else if ((ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_KEY_UP) && ev->index < UI_MAX_KEYS) {
        s.key_down[ev->index] = ev->kind == DB_INPUT_KEY_DOWN;
    }
    check_menu_chord();
    /* the device UI only takes input while it is what the deck shows, and nothing during an update */
    if (s.update_pct >= 0 || db_deck_writer() != DB_SOURCE_UI) {
        return;
    }
    static ui_model_t md; /* only the deck's event task gets here */
    model(&md);
    md.from_deck = db_deck_input_from_deck();
    ui_layout_t l = ui_layout_of(&s.dev);
    ui_action_t act;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool redraw = ui_menu_input(&s.menu, &md, &l, ev, &act);
    xSemaphoreGive(s.lock);
    carry_out(&act, &md);
    if (redraw || act.what != UI_DO_NOTHING) {
        /* the UI task switches the writer, once this event has reached every listener: a release that closes
         * the menu must not go on to the host that draws next */
        if (s.task) {
            xTaskNotifyGive(s.task);
        }
    }
}

static void on_device(const db_device_info_t *info)
{
    s.present = info != NULL;
    if (info) {
        s.dev = *info;
        s.full = true;
        s.lit = -1;
        db_deck_set_brightness(db_config()->brightness);
    }
    changed();
}

/* The chord must also fire while the encoders are held still, and the menu has its timers. */
static void tick(void *arg)
{
    static bool asking; /* a request runs out after a minute: the deck goes back to who draws */
    char ip[16];
    if (db_dock_request(ip) != asking) {
        asking = !asking;
        changed();
    }
    if (s.chord_since) {
        check_menu_chord();
    }
    static int64_t next;
    int64_t now = esp_timer_get_time();
    /* the password page, the reset question or a login request ran out: the deck goes back to who draws */
    static bool overlaid;
    if (overlay(now) != overlaid) {
        overlaid = !overlaid;
        changed();
    }
    static bool handing_over;
    if ((now < s.handover_until) != handing_over) {
        handing_over = !handing_over;
        changed();
    }
    static int64_t next_look;
    const db_config_t *c = db_config();
    if (now >= next_look && s.present && !c->satellite[0] && !c->satellite_auto_off &&
        db_deck_writer() == DB_SOURCE_UI) {
        next_look = now + 5000000;
        static size_t seen;
        db_net_found_t found[4];
        size_t n = db_net_companions(found, 4); /* also starts the next browse */
        if (n != seen) {
            seen = n;
            changed();
        }
    }
    if (now >= next) {
        next = now + 1000000;
        static ui_model_t md; /* kept off the esp_timer task's small stack */
        memset(&md, 0, sizeof(md));
        md.now_us = now;
        xSemaphoreTake(s.lock, portMAX_DELAY);
        bool redraw = ui_menu_tick(&s.menu, &md);
        xSemaphoreGive(s.lock);
        portENTER_CRITICAL(&login_mux);
        if (login_token[0] && now - login_made >= LOGIN_US) {
            login_token[0] = '\0';
            redraw = true;
        }
        portEXIT_CRITICAL(&login_mux);
        if (redraw) {
            changed();
        }
    }
}

#define UPGRADE_AGAIN_US (60 * 1000000LL)

static volatile uint32_t satellite_changes; /* per save: a set and a removal between looks count twice */

/* Companion that drives the deck as a dock host is moved to its Satellite API when that answers: all
 * controls, and no lost connections. The probe blocks for seconds, so it has its own task. */
static void upgrade_task(void *arg)
{
    struct {
        uint32_t ip;
        int64_t at;
    } tried[DB_DOCK_HOSTS] = {0}; /* Companion dock hosts asked at `at`, since the address last changed */
    uint32_t seen_changes = 0;
    uint32_t before[DB_DOCK_HOSTS] = {
        0}; /* the hosts of the last round: one that connects anew is asked at once */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        db_dock_host_t hosts[DB_DOCK_HOSTS];
        size_t n = db_dock_hosts(hosts, DB_DOCK_HOSTS);
        uint32_t was[DB_DOCK_HOSTS];
        memcpy(was, before, sizeof(was));
        memset(before, 0, sizeof(before));
        for (size_t i = 0; i < n; i++) {
            before[i] = ipaddr_addr(hosts[i].ip);
        }
        if (seen_changes != satellite_changes) {
            seen_changes = satellite_changes;
            memset(tried, 0, sizeof(tried));
        }
        const db_config_t *cfg = db_config();
        if (cfg->satellite[0] || cfg->satellite_auto_off || cfg->controller != DB_CTRL_COMPANION) {
            continue;
        }
        for (size_t i = 0; i < n; i++) {
            uint32_t ip = before[i];
            int64_t now = esp_timer_get_time();
            int slot = -1, free_slot = 0;
            bool anew = true;
            for (int k = 0; k < DB_DOCK_HOSTS; k++) {
                slot = tried[k].ip == ip ? k : slot;
                free_slot = tried[k].ip ? free_slot : k;
                anew &= was[k] != ip;
            }
            /* a dock host picked by hand stays on the dock path */
            if (hosts[i].kind != DB_CTRL_COMPANION || strcmp(cfg->host, hosts[i].ip) == 0 ||
                (slot >= 0 && !anew && now - tried[slot].at < UPGRADE_AGAIN_US)) {
                continue;
            }
            bool first = slot < 0;
            slot = first ? free_slot : slot;
            tried[slot].ip = ip;
            tried[slot].at = now;
            char addr[40], version[24];
            snprintf(addr, sizeof(addr), "%s", hosts[i].ip);
            const char *why = db_satellite_probe(addr, "", version, sizeof(version));
            if (why) {
                snprintf(addr, sizeof(addr), "ws://%s:16623", hosts[i].ip);
                why = db_satellite_probe(addr, "", version, sizeof(version)) ? why : NULL;
            }
            if (why) {
                if (first) {
                    db_log_event(TAG, "source",
                                 "Companion at %s has no Satellite API; Satellite would be better",
                                 hosts[i].ip);
                }
                continue;
            }
            db_config_t c;
            edit_begin(&c);
            /* a person was quicker */
            if (c.satellite[0] || c.satellite_auto_off || c.controller != DB_CTRL_COMPANION ||
                strcmp(c.host, hosts[i].ip) == 0) {
                db_config_edit_end();
                break;
            }
            snprintf(c.satellite, sizeof(c.satellite), "%s", addr);
            if (!c.host[0]) {
                snprintf(c.host, sizeof(c.host), "satellite");
            }
            if (edit_end(&c, "Companion address") == ESP_OK) {
                char name[DB_DNS_NAME_LEN];
                db_dock_name(ip, name);
                db_log_event(TAG, "source", "Companion%s%s at %s moved from Dock to Satellite",
                             name[0] ? " " : "", name, hosts[i].ip);
            }
            break;
        }
    }
}

esp_err_t db_ui_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.qr_lock = xSemaphoreCreateMutex();
    jpeg_port_init();
    if (!s.lock || !s.qr_lock ||
        xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 4, &s.task, 0) != pdPASS ||
        xTaskCreate(upgrade_task, "sat_upgrade", 8192, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    db_deck_listener_t l = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&l);
    const esp_timer_create_args_t targs = {.callback = tick, .name = "ui_tick"};
    esp_timer_handle_t timer;
    esp_timer_create(&targs, &timer);
    esp_timer_start_periodic(timer, 200000);
    const esp_timer_create_args_t save = {.callback = brightness_due, .name = "brightness_save"};
    esp_timer_create(&save, &s.save_timer);
    return ESP_OK;
}

/* *asked rate-limits: true when this call starts a new `us` long page. */
static bool start_page(int64_t *asked, int64_t *end, int64_t us)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&login_mux);
    bool fresh = now - *asked >= AGAIN_US;
    if (fresh) {
        *asked = now;
        *end = now + us;
    }
    portEXIT_CRITICAL(&login_mux);
    return fresh;
}

int db_ui_show_password(void)
{
    if (!s.present) {
        return 0;
    }
    if (start_page(&pass_asked, &pass_until, PASSWORD_US)) {
        ESP_LOGI(TAG, "web password shown on the deck");
        changed();
    }
    return 1;
}

esp_err_t db_ui_factory_reset(void)
{
    esp_err_t err = db_config_reset();
    esp_err_t dump = esp_core_dump_image_erase();
    esp_err_t page = db_ota_web_erase();
    err = err == ESP_OK && dump != ESP_ERR_NOT_FOUND ? dump : err;
    return err == ESP_OK && page != ESP_ERR_NOT_FOUND ? page : err;
}

int db_ui_ask_reset(void)
{
    if (!s.present) {
        return -1;
    }
    if (start_page(&reset_asked, &reset_until, RESET_ASK_US)) {
        ESP_LOGI(TAG, "the deck asks for a reset");
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.menu.confirm_key = -1; /* a tap armed on another page must not count here */
        xSemaphoreGive(s.lock);
        changed();
    }
    int64_t left = until(&reset_until) - esp_timer_get_time();
    return left > 0 ? (int)((left + 999999) / 1000000) : 0;
}

void db_ui_cancel_reset(void)
{
    portENTER_CRITICAL(&login_mux);
    reset_until = 0;
    portEXIT_CRITICAL(&login_mux);
    changed();
}

bool db_ui_ask_login(uint32_t ip, const char *who, int seconds)
{
    if (!s.present) {
        return false;
    }
    uint8_t mac[6];
    db_dock_mac(ip, mac);
    char mac_s[18] = "";
    if (memcmp(mac, (uint8_t[6]){0}, 6) != 0) {
        snprintf(mac_s, sizeof(mac_s), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
                 mac[4], mac[5]);
    }
    char ip_s[16];
    ip4addr_ntoa_r((const ip4_addr_t *)&ip, ip_s, sizeof(ip_s));
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.menu.keys_down = 0; /* a key held before the page came must not answer it on release */
    s.menu.encoders_down = 0;
    xSemaphoreGive(s.lock);
    portENTER_CRITICAL(&login_mux);
    ask.until = esp_timer_get_time() + (int64_t)seconds * 1000000;
    ask.seq++;
    ask.ip = ip;
    ask.answer = -1;
    strcpy(ask.ip_s, ip_s);
    strcpy(ask.mac, mac_s);
    snprintf(ask.who, sizeof(ask.who), "%s", who);
    portEXIT_CRITICAL(&login_mux);
    ESP_LOGI(TAG, "login request from %s on the deck", ip_s);
    changed();
    return true;
}

int db_ui_login_answer(void)
{
    portENTER_CRITICAL(&login_mux);
    int a = ask.answer;
    portEXIT_CRITICAL(&login_mux);
    return a;
}

void db_ui_end_login(void)
{
    portENTER_CRITICAL(&login_mux);
    ask.until = 0;
    portEXIT_CRITICAL(&login_mux);
    changed();
}

void db_ui_set_active_link(int index)
{
    s.active_link = index;
    changed();
}

void db_ui_hosts_changed(void)
{
    changed();
}

void db_ui_set_update(int pct, bool wlan_chip)
{
    s.update_pct = pct;
    s.update_wlan = wlan_chip;
    changed();
}

void db_ui_refresh(void)
{
    static char satellite[sizeof(db_config()->satellite)];
    static bool auto_off;
    const db_config_t *c = db_config();
    if (strcmp(satellite, c->satellite) != 0 || auto_off != c->satellite_auto_off) {
        strcpy(satellite, c->satellite);
        auto_off = c->satellite_auto_off;
        satellite_changes++;
    }
    s.lit = -1; /* PUT /api/config may have set the brightness on the deck */
    changed();
}

void db_ui_set_c6_version(const char *version)
{
    snprintf(s.c6, sizeof(s.c6), "%s", version ? version : "");
    changed();
}

/* *complete is false when an image that is still shown could not be drawn, e.g. out of memory. */
static esp_err_t render_snapshot(const db_device_info_t *dev, const db_shot_t *shots, size_t n,
                                 uint8_t **jpeg, size_t *len, bool *complete)
{
    *complete = true;
    const db_display_t *d = &dev->displays[0];
    uint8_t *px = jpeg_port_alloc_rgb(d->width, d->height);
    if (!px) {
        return ESP_ERR_NO_MEM;
    }
    memset(px, 0, (size_t)d->width * d->height * 3);
    for (size_t i = 0; i < n; i++) {
        uint8_t *data = NULL;
        size_t dlen = 0;
        esp_err_t err = db_deck_shot_copy(shots[i].id, &data, &dlen);
        if (err == ESP_ERR_NOT_FOUND) {
            continue; /* replaced meanwhile; the newer one is in the list too */
        }
        if (err == ESP_OK) {
            err = jpeg_port_decode_for(d, data, dlen, px, d->width, d->height, shots[i].rect);
        }
        heap_caps_free(data);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "snapshot: image %lu not drawn: %s", (unsigned long)shots[i].id,
                     esp_err_to_name(err));
            *complete = false;
        }
    }
    esp_err_t err = jpeg_port_encode(px, d->width, d->height, QUALITY, jpeg, len);
    heap_caps_free(px);
    return err;
}

esp_err_t db_ui_snapshot(uint8_t **jpeg, size_t *len)
{
    static db_device_info_t dev; /* too big for the web server's stack */
    static db_shot_t shots[DB_SCREEN_MAX];
    static SemaphoreHandle_t busy;
    static uint8_t *cached; /* cameras polling an unchanged deck get a copy, not a render */
    static size_t cached_len;
    static uint32_t cached_key;
    if (!busy) {
        busy = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(busy, portMAX_DELAY);
    esp_err_t err = db_deck_info(&dev) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        db_source_t writer;
        size_t n = db_deck_shots(&writer, shots, DB_SCREEN_MAX);
        uint32_t key = db_deck_shots_key(writer, shots, n);
        if (!cached || key != cached_key) {
            heap_caps_free(cached);
            cached = NULL;
            bool complete;
            err = render_snapshot(&dev, shots, n, &cached, &cached_len, &complete);
            cached_key = complete ? key : key + 1; /* a picture with a hole is sent once, then drawn again */
        }
    }
    if (err == ESP_OK) {
        *jpeg = heap_caps_malloc(cached_len, MALLOC_CAP_SPIRAM);
        err = *jpeg ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        memcpy(*jpeg, cached, cached_len);
        *len = cached_len;
    }
    xSemaphoreGive(busy);
    return err;
}
