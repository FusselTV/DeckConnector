#include "db_ui.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_link.h"
#include "db_net.h"
#include "db_satellite.h"
#include "esp_app_desc.h"
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
#define REDRAW_DELAY  10 /* ms: the events of one turn or press come in a burst */
#define FULL_ABOVE    5       /* more changed areas than this go out as one full image, measured faster */
#define SAVE_AFTER_US 1500000 /* brightness goes to flash once it rests */

static const char *TAG = "ui";

static struct {
    TaskHandle_t task;
    SemaphoreHandle_t lock; /* menu, between the deck's event task and the UI task */
    int active_link;
    int update_pct;
    char update_what[24];
    char c6[16];
    db_device_info_t dev;
    bool present;
    ui_canvas_t canvas;
    uint32_t seq;
    bool enc_down[8];
    bool key_down[32];
    int64_t chord_since;
    bool covered;
    bool full;         /* the next picture goes out whole: someone else drew meanwhile */
    ui_screen_t shown; /* what is on the deck, to send only what changed */
    ui_menu_t menu;
    volatile int brightness; /* applied but not saved yet, -1 when saved */
    esp_timer_handle_t save_timer;
} s = {.active_link = -1, .update_pct = -1, .full = true, .menu = {.confirm_key = -1}, .brightness = -1};

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
    uint8_t diff = !wanted || strlen(token) != 16 || !login_token[0] || esp_timer_get_time() - login_made >= LOGIN_US;
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
    portENTER_CRITICAL(&login_mux);
    bool shown = wanted && login_token[0] != '\0';
    portEXIT_CRITICAL(&login_mux);
    return shown;
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
    bool want = !c->setup_done && !c->qr_login_off && ip[0];
    char fresh[17];
    uint8_t raw[8];
    esp_fill_random(raw, sizeof(raw));
    for (int i = 0; i < 8; i++) {
        snprintf(fresh + 2 * i, 3, "%02x", raw[i]);
    }
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

#define OFFER_DENIED_MAX 4
static char denied[OFFER_DENIED_MAX][16]; /* Companions the deck was told "no" for, until a restart */

/* A Companion that announces its Satellite API while no address is set and nobody said no to it. */
static void model_offer(ui_model_t *md, const db_config_t *c)
{
    if (c->satellite[0]) {
        return;
    }
    db_net_found_t found[OFFER_DENIED_MAX];
    size_t n = db_net_companions(found, OFFER_DENIED_MAX);
    for (size_t i = 0; i < n; i++) {
        bool no = false;
        for (int d = 0; d < OFFER_DENIED_MAX; d++) {
            no |= strcmp(denied[d], found[i].host) == 0;
        }
        if (!no) {
            /* "Companion (studio)" -> "studio": the key already says Companion */
            const char *name = found[i].name[0] ? found[i].name : found[i].host;
            size_t len = strlen(name);
            bool wrapped = strncmp(name, "Companion (", 11) == 0 && len > 12 && name[len - 1] == ')';
            snprintf(md->offer_name, sizeof(md->offer_name), "%.*s", wrapped ? (int)len - 12 : (int)len,
                     wrapped ? name + 11 : name);
            snprintf(md->offer_addr, sizeof(md->offer_addr), "%s:%u", found[i].host, found[i].port);
            return;
        }
    }
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
    };
    snprintf(md->menu_chord, sizeof(md->menu_chord), "%s", c->menu_chord);
    model_offer(md, c);
    snprintf(md->lan_ip, sizeof(md->lan_ip), "%s", n.lan_ip);
    snprintf(md->wlan_ip, sizeof(md->wlan_ip), "%s", n.wlan_ip);
    snprintf(md->wifi_ssid, sizeof(md->wifi_ssid), "%s", c->wifi_ssid);
    snprintf(md->ap_ssid, sizeof(md->ap_ssid), "%s", n.ap_ssid);
    snprintf(md->ap_pass, sizeof(md->ap_pass), "%s", n.ap_pass);
    snprintf(md->ap_ip, sizeof(md->ap_ip), "%s", n.ap_ip);
    model_qr(md);
    model_login(md, c);
    if (db_dock_request(md->request)) {
        db_dock_name(ipaddr_addr(md->request), md->request_name);
    }
    snprintf(md->server_url, sizeof(md->server_url), "%s", db_config_server_of(c, 0));
    model_sources(md);
    snprintf(md->web_pass, sizeof(md->web_pass), "%s", c->admin_pass);
    snprintf(md->fw, sizeof(md->fw), "%s", esp_app_get_description()->version);
    snprintf(md->wlan_fw, sizeof(md->wlan_fw), "%s", s.c6);
    snprintf(md->update_what, sizeof(md->update_what), "%s", s.update_what);
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
        s.menu.page == PAGE_WELCOME_NET) {
        return DB_SOURCE_UI;
    }
    return db_ui_chosen_source();
}

static void pick_writer(void)
{
    /* The menu or an update drew over the active server's picture; tell it to draw again once they end. */
    bool covering = (ui_menu_open(&s.menu) || s.update_pct >= 0) && s.active_link >= 0;
    db_source_t wanted = wanted_writer();
    if (wanted == DB_SOURCE_UI && db_deck_writer() != DB_SOURCE_UI) {
        s.full = true;
    }
    db_deck_set_writer(wanted);
    if (s.covered && !covering && s.active_link >= 0) {
        db_link_resend_role();
    }
    s.covered = covering;
}

static void changed(void)
{
    pick_writer();
    xTaskNotifyGive(s.task);
}

static void save_brightness(void *arg)
{
    int b = s.brightness;
    if (b < 0) {
        return;
    }
    db_config_t c;
    db_config_copy(&c);
    c.brightness = (uint8_t)b;
    db_config_save(&c);
    if (s.brightness == b) { /* a turn during the save keeps its value for the next save */
        s.brightness = -1;
    }
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static void save_lan(const ui_action_t *act)
{
    db_config_t c;
    db_config_copy(&c);
    c.lan.dhcp = act->dhcp;
    char *fields[] = {c.lan.ip, c.lan.mask, c.lan.gateway, c.lan.dns};
    for (int f = 0; f < 4; f++) {
        const uint8_t *o = act->octets[f];
        snprintf(fields[f], 16, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
    }
    if (db_config_save(&c) == ESP_OK) {
        xTaskCreate(restart_task, "ui_restart", 2048, NULL, 5, NULL);
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
        db_config_copy(&c);
        c.controller = (db_controller_t)md->sources[act->value].kind;
        snprintf(c.host, sizeof(c.host), "%s", md->sources[act->value].id);
        c.setup_done = true;
        db_config_save(&c);
        break;
    case UI_DO_OFFER:
        if (!act->value) {
            static int next;
            snprintf(denied[next++ % OFFER_DENIED_MAX], sizeof(denied[0]), "%.*s",
                     (int)strcspn(md->offer_addr, ":"), md->offer_addr);
            break;
        }
        db_config_copy(&c);
        snprintf(c.satellite, sizeof(c.satellite), "%s", md->offer_addr);
        c.controller = DB_CTRL_COMPANION;
        snprintf(c.host, sizeof(c.host), "satellite");
        c.setup_done = true;
        ESP_LOGI(TAG, "Companion %s over Satellite, chosen on the deck", md->offer_addr);
        db_config_save(&c);
        break;
    case UI_DO_BRIGHTNESS: {
        int b = md->brightness + act->value;
        s.brightness = b < 0 ? 0 : (b > 100 ? 100 : b);
        db_deck_set_brightness((uint8_t)s.brightness);
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
        db_config_reset();
        xTaskCreate(restart_task, "ui_restart", 2048, NULL, 5, NULL);
        break;
    case UI_DO_RESTART:
        xTaskCreate(restart_task, "ui_restart", 2048, NULL, 5, NULL);
        break;
    case UI_DO_SAVE_LAN:
        save_lan(act);
        break;
    case UI_DO_SETUP_DONE:
        db_config_copy(&c);
        c.setup_done = true;
        db_config_save(&c);
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
    esp_err_t err = jpeg_port_encode(src, r.w, r.h, QUALITY, &jpeg, &len);
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

static void ui_task(void *arg)
{
    static ui_screen_t sc; /* 2 KB, kept off the task stack */
    static ui_model_t md;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(REDRAW_DELAY));
        ulTaskNotifyTake(pdTRUE, 0);
        pick_writer();
        if (!s.present || db_deck_writer() != DB_SOURCE_UI) {
            continue;
        }
        model(&md);
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

static void check_menu_chord(void)
{
    int last;
    bool on_encoders;
    bool any = db_ui_menu_chord(s.dev.encoder_count, s.dev.key_count, &on_encoders, &last);
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
    } else if (ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_KEY_UP) {
        s.key_down[ev->index & 31] = ev->kind == DB_INPUT_KEY_DOWN;
    }
    check_menu_chord();
    /* the device UI only takes input while it is what the deck shows, and nothing during an update */
    if (s.update_pct >= 0 || db_deck_writer() != DB_SOURCE_UI) {
        return;
    }
    static ui_model_t md; /* only the deck's event task gets here */
    model(&md);
    ui_layout_t l = ui_layout_of(&s.dev);
    ui_action_t act;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool redraw = ui_menu_input(&s.menu, &md, &l, ev, &act);
    xSemaphoreGive(s.lock);
    carry_out(&act, &md);
    if (redraw || act.what != UI_DO_NOTHING) {
        /* the UI task switches the writer, once this event has reached every listener: a release that closes
         * the menu must not go on to the host that draws next */
        xTaskNotifyGive(s.task);
    }
}

static void on_device(const db_device_info_t *info)
{
    s.present = info != NULL;
    if (info) {
        s.dev = *info;
        s.full = true;
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
    static int64_t next_look;
    if (now >= next_look && s.present && !db_config()->satellite[0] && db_deck_writer() == DB_SOURCE_UI) {
        next_look = now + 5000000;
        static size_t seen;
        db_net_found_t found[OFFER_DENIED_MAX];
        size_t n = db_net_companions(found, OFFER_DENIED_MAX); /* also starts the next browse */
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

esp_err_t db_ui_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    jpeg_port_init();
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 4, &s.task, 0);
    db_deck_listener_t l = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&l);
    db_net_on_change(changed);
    const esp_timer_create_args_t targs = {.callback = tick, .name = "ui_tick"};
    esp_timer_handle_t timer;
    esp_timer_create(&targs, &timer);
    esp_timer_start_periodic(timer, 200000);
    const esp_timer_create_args_t save = {.callback = save_brightness, .name = "brightness_save"};
    esp_timer_create(&save, &s.save_timer);
    return ESP_OK;
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

void db_ui_set_update(int pct, const char *what)
{
    s.update_pct = pct;
    snprintf(s.update_what, sizeof(s.update_what), "%s", what ? what : "");
    changed();
}

void db_ui_refresh(void)
{
    changed();
}

void db_ui_set_c6_version(const char *version)
{
    snprintf(s.c6, sizeof(s.c6), "%s", version ? version : "");
    changed();
}

/* Mirror one rectangle in both directions: decks with flipped displays get their images upside down. */
static void turn_around(uint8_t *px, uint16_t w, db_rect_t r)
{
    for (int y = 0; y < r.h / 2 + (r.h & 1); y++) {
        int y2 = r.h - 1 - y;
        for (int x = 0; x < (y == y2 ? r.w / 2 : r.w); x++) {
            uint8_t *a = px + ((size_t)(r.y + y) * w + r.x + x) * 3;
            uint8_t *b = px + ((size_t)(r.y + y2) * w + r.x + r.w - 1 - x) * 3;
            uint8_t t[3];
            memcpy(t, a, 3);
            memcpy(a, b, 3);
            memcpy(b, t, 3);
        }
    }
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
            err =
                jpeg_port_decode_into(data, dlen, px, d->width, d->height, shots[i].rect.x, shots[i].rect.y);
        }
        heap_caps_free(data);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "snapshot: image %lu not drawn: %s", (unsigned long)shots[i].id,
                     esp_err_to_name(err));
            *complete = false;
        } else if (d->flip_x && d->flip_y) {
            turn_around(px, d->width, shots[i].rect);
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
