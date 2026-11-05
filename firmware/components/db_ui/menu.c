#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_link.h"
#include "db_net.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icons.h"
#include "lwip/ip4_addr.h"

#define IDLE_CLOSE_US      60000000
#define RESET_HOLD_US      5000000
#define CONFIRM_US         3000000
#define BRIGHTNESS_STEP    5
#define SAVE_AFTER_US      1500000 /* brightness goes to flash once the encoder rests */
#define C_ACTIVE           0x30c060
#define C_EDIT             0x2f7cf6
#define C_DANGER           0xe5484d
#define C_WARN             0xffb020
#define C_DIM              0x9a9aa0
#define KEY_MENU           3
#define KEY_LAN            4
#define KEY_WLAN           5
#define KEY_RESTART        7
#define ENCODER_BRIGHTNESS 0
#define ENCODER_NAV        3

static const char *TAG = "menu";

typedef enum { PAGE_CLOSED, PAGE_MAIN, PAGE_LAN, PAGE_SETUP_AP } page_t;

static const char *const field_names[] = {"IP", "Maske", "Gateway", "DNS"};
static const db_controller_t controllers[] = {DB_CTRL_SERVER, DB_CTRL_COMPANION, DB_CTRL_APP};

static struct {
    void (*changed)(void);
    page_t page;
    int64_t last_input;
    int64_t restart_down;
    int64_t confirm_until;
    int confirm_key; /* the key waiting for its second tap */
    char note[64];
    bool lan_dhcp;
    int field;
    uint8_t octets[4][4];
    uint8_t loaded[4][4];
    bool loaded_dhcp;
    uint32_t keys_down; /* pressed while the UI had the deck */
    uint8_t encoders_down;
    volatile int brightness; /* shown and applied; saved once the encoder rests, -1 when saved */
    esp_timer_handle_t save_timer;
} m = {.brightness = -1, .confirm_key = -1};

bool ui_menu_open(void)
{
    return m.page != PAGE_CLOSED;
}

static void go(page_t p)
{
    m.page = p;
    m.note[0] = '\0';
    m.confirm_until = 0;
    m.confirm_key = -1;
    m.restart_down = 0;
    m.keys_down = 0; /* a press from the page before must not act here */
    m.encoders_down = 0;
    m.last_input = esp_timer_get_time();
}

void ui_menu_open_main(void)
{
    go(PAGE_MAIN);
}

static void restart_soon(void)
{
    m.changed();
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static int brightness(void)
{
    int b = m.brightness;
    return b >= 0 ? b : db_config()->brightness;
}

static void save_brightness(void *arg)
{
    int b = m.brightness;
    if (b < 0) {
        return;
    }
    db_config_t c;
    db_config_copy(&c);
    c.brightness = (uint8_t)b;
    db_config_save(&c);
    if (m.brightness == b) { /* a turn during the save keeps its value for the next save */
        m.brightness = -1;
    }
}

static void turn_brightness(int ticks)
{
    int b = brightness() + ticks * BRIGHTNESS_STEP;
    m.brightness = b < 0 ? 0 : (b > 100 ? 100 : b);
    db_deck_set_brightness((uint8_t)m.brightness);
    esp_timer_stop(m.save_timer);
    esp_timer_start_once(m.save_timer, SAVE_AFTER_US);
}

static void pick_controller(db_controller_t ctrl)
{
    db_config_t c;
    db_config_copy(&c);
    if (c.controller != ctrl) {
        c.controller = ctrl;
        db_config_save(&c);
    }
}

static bool confirming(int key)
{
    return m.confirm_key == key && esp_timer_get_time() < m.confirm_until;
}

/* First tap arms, a second one within 3 s does it. */
static bool confirmed(int key)
{
    if (confirming(key)) {
        m.confirm_key = -1;
        return true;
    }
    m.confirm_key = key;
    m.confirm_until = esp_timer_get_time() + CONFIRM_US;
    return false;
}

static void button(ui_tile_t *t, const char *icon, const char *label, const char *sub)
{
    *t = (ui_tile_t){.kind = TILE_BUTTON, .icon = icon};
    snprintf(t->label, sizeof(t->label), "%s", label);
    snprintf(t->sub, sizeof(t->sub), "%s", sub);
}

static void card(ui_tile_t *t, const char *icon, const char *label, const char *value, const char *sub)
{
    *t = (ui_tile_t){.kind = TILE_CARD, .icon = icon};
    snprintf(t->label, sizeof(t->label), "%s", label);
    snprintf(t->value, sizeof(t->value), "%s", value);
    snprintf(t->sub, sizeof(t->sub), "%s", sub);
}

static void close_key(ui_tile_t *t, const char *icon, const char *label, const char *sub)
{
    button(t, icon, label, sub);
    t->badge = BADGE_CLOSE;
    t->color = C_EDIT;
}

static void segment(ui_segment_t *s, int span, const char *icon, uint32_t icon_color, const char *label,
                    const char *value, const char *sub)
{
    *s = (ui_segment_t){.span = (uint8_t)span, .icon = icon, .icon_color = icon_color, .bar = -1};
    snprintf(s->label, sizeof(s->label), "%s", label);
    snprintf(s->value, sizeof(s->value), "%s", value);
    snprintf(s->sub, sizeof(s->sub), "%s", sub);
}

static void brightness_segment(ui_segment_t *s)
{
    char v[16];
    snprintf(v, sizeof(v), "%d %%", brightness());
    segment(s, 1, ICON_BRIGHTNESS, C_WARN, "Helligkeit · drehen", v, "");
    s->bar = brightness();
}

static const char *controller_label(db_controller_t c)
{
    return c == DB_CTRL_SERVER ? "Server" : (c == DB_CTRL_APP ? "Elgato App" : "Companion");
}

static void controller_tiles(ui_screen_t *sc, const ui_state_t *st, bool ask)
{
    for (int i = 0; i < 3; i++) {
        db_controller_t c = controllers[i];
        bool up = c == DB_CTRL_SERVER ? db_link_connected(0) || db_link_connected(1)
                                      : (c == DB_CTRL_APP ? st->app_client : st->companion_client);
        const char *icon = c == DB_CTRL_SERVER ? ICON_SERVER : (c == DB_CTRL_APP ? ICON_APP : ICON_COMPANION);
        bool ask_now = ask && confirming(i);
        button(&sc->tiles[i], icon, ask_now ? "Wechseln?" : controller_label(c),
               ask_now ? "nochmal tippen" : (up ? "verbunden" : "getrennt"));
        sc->tiles[i].active = db_config()->controller == c || ask_now;
        sc->tiles[i].color = ask_now ? C_EDIT : C_ACTIVE;
    }
}

static void lan_card(ui_tile_t *t, const db_net_status_t *n)
{
    const char *sub = db_config()->lan.dhcp ? "per DHCP" : "feste Adresse";
    card(t, ICON_LAN, "LAN", n->lan_up ? n->lan_ip : (n->lan_link ? "keine Adresse" : "kein Kabel"), sub);
}

static void wlan_card(ui_tile_t *t, const db_net_status_t *n)
{
    const db_config_t *c = db_config();
    card(t, n->wlan_up ? ICON_WLAN : ICON_WLAN_OFF, "WLAN",
         n->wlan_up ? n->wlan_ip : (c->wifi_ssid[0] ? "getrennt" : "aus"),
         c->wifi_ssid[0] ? c->wifi_ssid : "kein Netz");
}

static void home_banner(ui_segment_t *s, const db_net_status_t *n)
{
    const db_config_t *c = db_config();
    char sub[112];
    if (!n->lan_up && !n->wlan_up) {
        segment(s, 3, ICON_NO_NETWORK, C_DANGER, "Netzwerk", "Kein Netz",
                "LAN-Kabel stecken. Nach 1 min startet das Setup-WLAN.");
    } else if (c->controller == DB_CTRL_COMPANION) {
        snprintf(sub, sizeof(sub), "In Companion als Remote Surface eintragen: %s",
                 n->lan_up ? n->lan_ip : n->wlan_ip);
        segment(s, 3, ICON_ALERT, C_WARN, "Companion", "Warte auf Companion", sub);
    } else if (c->controller == DB_CTRL_APP) {
        segment(s, 3, ICON_ALERT, C_WARN, "Elgato App", "Warte auf die Elgato App",
                "Die Stream Deck App findet die Bridge im Netz selbst");
    } else if (db_link_connected(0) || db_link_connected(1)) {
        segment(s, 3, ICON_SERVER, C_ACTIVE, "Server", "Server verbunden", "Er zeichnet gleich");
    } else {
        snprintf(sub, sizeof(sub), "%s", c->servers[0][0] ? c->servers[0] : "Sucht _deckbridge._tcp im Netz");
        segment(s, 3, ICON_ALERT, C_WARN, "Server", c->servers[0][0] ? "Server getrennt" : "Suche Server",
                sub);
    }
}

static void home_screen(ui_screen_t *sc, const ui_state_t *st)
{
    db_net_status_t n;
    db_net_status(&n);
    controller_tiles(sc, st, true);
    button(&sc->tiles[KEY_MENU], ICON_SETTINGS, "Menü", "Einstellungen");
    sc->tiles[KEY_MENU].badge = BADGE_OPEN;
    lan_card(&sc->tiles[4], &n);
    wlan_card(&sc->tiles[5], &n);
    char ver[32];
    char chip[24];
    snprintf(ver, sizeof(ver), "%s", esp_app_get_description()->version);
    char *hash = strstr(ver, "-g"); /* "v0.1.0-20-ge4fdacd-dirty": the build count is enough on a key */
    if (hash) {
        *hash = '\0';
    }
    snprintf(chip, sizeof(chip), "WLAN %s", st->c6 && st->c6[0] ? st->c6 : "-");
    card(&sc->tiles[6], ICON_FIRMWARE, "Firmware", ver, chip);
    brightness_segment(&sc->seg[0]);
    home_banner(&sc->seg[1], &n);
}

/* Home sits where the chosen host draws when it is back, so a stray tap there must not switch the host. */
static bool home_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_ENCODER_TURN && ev->index == ENCODER_BRIGHTNESS) {
        turn_brightness(ev->value);
        return true;
    }
    if (ev->kind != DB_INPUT_KEY_UP) {
        return false;
    }
    if (ev->index < 3) {
        if (db_config()->controller != controllers[ev->index] && confirmed(ev->index)) {
            pick_controller(controllers[ev->index]);
        }
        return true;
    }
    if (ev->index == KEY_MENU) {
        go(PAGE_MAIN);
        return true;
    }
    return false;
}

static void main_screen(ui_screen_t *sc, const ui_state_t *st)
{
    db_net_status_t n;
    db_net_status(&n);
    controller_tiles(sc, st, false);
    close_key(&sc->tiles[KEY_MENU], ICON_SETTINGS, "Menü", "schließen");
    lan_card(&sc->tiles[KEY_LAN], &n);
    sc->tiles[KEY_LAN].badge = BADGE_OPEN;
    button(&sc->tiles[KEY_WLAN], ICON_SETUP_AP, "Setup-WLAN", "starten");
    sc->tiles[KEY_WLAN].badge = BADGE_OPEN;
    int64_t held = m.restart_down ? esp_timer_get_time() - m.restart_down : 0;
    char sub[24];
    if (held >= RESET_HOLD_US) {
        button(&sc->tiles[KEY_RESTART], ICON_RESET, "Werkszustand", "loslassen");
    } else if (held >= 1000000) {
        snprintf(sub, sizeof(sub), "in %d s", (int)((RESET_HOLD_US - held) / 1000000) + 1);
        button(&sc->tiles[KEY_RESTART], ICON_RESET, "Werkszustand", sub);
    } else if (confirming(KEY_RESTART)) {
        button(&sc->tiles[KEY_RESTART], ICON_RESTART, "Wirklich?", "nochmal tippen");
    } else {
        button(&sc->tiles[KEY_RESTART], ICON_RESTART, "Neustart", "5 s: Werkszustand");
    }
    sc->tiles[KEY_RESTART].active = held >= 1000000 || confirming(KEY_RESTART);
    sc->tiles[KEY_RESTART].color = C_DANGER;
    brightness_segment(&sc->seg[0]);
    char web[48];
    char login[72];
    snprintf(web, sizeof(web), "http://%s", n.lan_up ? n.lan_ip : n.wlan_ip);
    snprintf(login, sizeof(login), "Benutzer admin · Passwort %s", db_config()->admin_pass);
    segment(&sc->seg[1], 2, ICON_WEB, C_DIM, "Web-Zugang", web, m.note[0] ? m.note : login);
    segment(&sc->seg[3], 1, ICON_CLOSE, C_DIM, "", "Schließen", "tippen oder drehen");
}

static void restart_key_up(void)
{
    int64_t held = esp_timer_get_time() - m.restart_down;
    m.restart_down = 0;
    if (held >= RESET_HOLD_US) {
        ESP_LOGW(TAG, "factory reset from the menu");
        db_config_reset();
        snprintf(m.note, sizeof(m.note), "Werkszustand, starte neu");
        restart_soon();
    } else if (held < 1000000 && confirmed(KEY_RESTART)) {
        snprintf(m.note, sizeof(m.note), "Starte neu");
        restart_soon();
    }
}

static void lan_load(void)
{
    db_ip_config_t c = db_config()->lan;
    db_net_status_t n;
    db_net_status(&n);
    m.lan_dhcp = c.dhcp;
    m.field = 0;
    if (c.ip[0] == '\0' && n.lan_ip[0]) {
        snprintf(c.ip, sizeof(c.ip), "%s", n.lan_ip); /* start from the address DHCP gave */
    }
    if (c.mask[0] == '\0') {
        snprintf(c.mask, sizeof(c.mask), "255.255.255.0");
    }
    char *fields[] = {c.ip, c.mask, c.gateway, c.dns};
    for (int f = 0; f < 4; f++) {
        ip4_addr_t a = {0};
        ip4addr_aton(fields[f], &a);
        for (int o = 0; o < 4; o++) {
            m.octets[f][o] = (uint8_t)(ip4_addr_get_u32(&a) >> (8 * o));
        }
    }
    memcpy(m.loaded, m.octets, sizeof(m.loaded));
    m.loaded_dhcp = m.lan_dhcp;
}

/* Closing acts on release, so the release does not reach the host that draws next. */
static bool main_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_ENCODER_TURN && ev->index == ENCODER_BRIGHTNESS) {
        turn_brightness(ev->value);
        return true;
    }
    bool nav =
        (ev->kind == DB_INPUT_ENCODER_TURN || ev->kind == DB_INPUT_ENCODER_UP) && ev->index == ENCODER_NAV;
    if (nav || (ev->kind == DB_INPUT_TOUCH_TAP && ev->x >= 600)) {
        go(PAGE_CLOSED);
        return true;
    }
    if (ev->kind == DB_INPUT_KEY_DOWN && ev->index == KEY_RESTART) {
        m.restart_down = esp_timer_get_time();
        return true;
    }
    if (ev->kind != DB_INPUT_KEY_UP) {
        return false;
    }
    switch (ev->index) {
    case 0:
    case 1:
    case 2:
        pick_controller(controllers[ev->index]);
        go(PAGE_CLOSED);
        return true;
    case KEY_MENU:
        go(PAGE_CLOSED);
        return true;
    case KEY_LAN:
        lan_load();
        go(PAGE_LAN);
        return true;
    case KEY_WLAN:
        if (db_net_ap_start() == ESP_OK) {
            go(PAGE_SETUP_AP);
        } else {
            snprintf(m.note, sizeof(m.note), "Kein WLAN-Chip");
        }
        return true;
    case KEY_RESTART:
        if (m.restart_down) {
            restart_key_up();
        }
        return true;
    default:
        return false;
    }
}

static void octets_str(const uint8_t *o, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
}

static void lan_screen(ui_screen_t *sc)
{
    for (int f = 0; f < 4; f++) {
        char v[20];
        octets_str(m.octets[f], v, sizeof(v));
        button(&sc->tiles[f], ICON_IP, field_names[f], m.lan_dhcp ? "per DHCP" : v);
        sc->tiles[f].active = !m.lan_dhcp && f == m.field;
        sc->tiles[f].color = C_EDIT;
        sc->tiles[f].dim = m.lan_dhcp;
    }
    close_key(&sc->tiles[KEY_LAN], ICON_LAN, "LAN", "verwerfen");
    button(&sc->tiles[5], m.lan_dhcp ? ICON_DHCP : ICON_FIXED, m.lan_dhcp ? "DHCP" : "Fest",
           m.lan_dhcp ? "tippen: fest" : "tippen: DHCP");
    button(&sc->tiles[7], ICON_SAVE, "Speichern", "startet neu");
    sc->tiles[7].active = true;
    sc->tiles[7].color = C_ACTIVE;
    if (m.lan_dhcp) {
        segment(&sc->seg[0], 4, ICON_LAN, C_DIM, "LAN", "Adresse per DHCP",
                m.note[0] ? m.note : "Die Adresse kommt vom Router");
        return;
    }
    for (int o = 0; o < 4; o++) {
        char label[24];
        char v[12];
        snprintf(label, sizeof(label), "%s · %d. Stelle", field_names[m.field], o + 1);
        snprintf(v, sizeof(v), "%u", m.octets[m.field][o]);
        segment(&sc->seg[o], 1, NULL, 0, label, v,
                o == 0 ? "drehen" : (o == 3 ? "drücken: nächstes Feld" : ""));
    }
}

static void lan_save(void)
{
    if (m.lan_dhcp == m.loaded_dhcp && (m.lan_dhcp || memcmp(m.octets, m.loaded, sizeof(m.loaded)) == 0)) {
        go(PAGE_MAIN);
        snprintf(m.note, sizeof(m.note), "LAN unverändert");
        return;
    }
    db_config_t c;
    db_config_copy(&c);
    c.lan.dhcp = m.lan_dhcp;
    char *fields[] = {c.lan.ip, c.lan.mask, c.lan.gateway, c.lan.dns};
    for (int f = 0; f < 4; f++) {
        octets_str(m.octets[f], fields[f], 16);
    }
    if (db_config_save(&c) == ESP_OK) {
        snprintf(m.note, sizeof(m.note), "Gespeichert, starte neu");
        restart_soon();
    }
}

static bool lan_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_ENCODER_TURN && ev->index < 4 && !m.lan_dhcp) {
        m.octets[m.field][ev->index] = (uint8_t)(m.octets[m.field][ev->index] + ev->value);
        return true;
    }
    if (ev->kind == DB_INPUT_ENCODER_DOWN && !m.lan_dhcp) {
        m.field = (m.field + 1) % 4;
        return true;
    }
    if (ev->kind != DB_INPUT_KEY_UP) {
        return false;
    }
    if (ev->index < 4 && !m.lan_dhcp) {
        m.field = ev->index;
    } else if (ev->index == KEY_LAN) {
        go(PAGE_MAIN);
    } else if (ev->index == 5) {
        m.lan_dhcp = !m.lan_dhcp;
    } else if (ev->index == 7) {
        lan_save();
    } else {
        return false;
    }
    return true;
}

static void ap_screen(ui_screen_t *sc)
{
    db_net_status_t n;
    db_net_status(&n);
    card(&sc->tiles[0], ICON_SETUP_AP, "WLAN-Name", n.ap_ssid, "");
    card(&sc->tiles[1], ICON_PASSWORD, "WLAN-Passwort", n.ap_pass, "");
    card(&sc->tiles[2], ICON_WEB, "Dann öffnen", n.ap_ip, "im Browser");
    card(&sc->tiles[3], ICON_PASSWORD, "Web-Login", db_config()->admin_pass, "Benutzer admin");
    close_key(&sc->tiles[KEY_WLAN], ICON_SETUP_AP, "Setup-WLAN", "ausschalten");
    segment(&sc->seg[0], 4, ICON_SETUP_AP, C_ACTIVE, "Setup-WLAN", "Setup-WLAN ist an",
            "Mit dem Handy verbinden und die Adresse im Browser öffnen");
}

/* A release only counts when its press happened here: releasing the menu chord must not act. */
static bool pressed_here(const db_input_t *ev)
{
    uint32_t key = 1u << (ev->index & 31);
    uint8_t enc = (uint8_t)(1u << (ev->index & 7));
    switch (ev->kind) {
    case DB_INPUT_KEY_DOWN:
        m.keys_down |= key;
        return true;
    case DB_INPUT_KEY_UP: {
        bool here = m.keys_down & key;
        m.keys_down &= ~key;
        return here;
    }
    case DB_INPUT_ENCODER_DOWN:
        m.encoders_down |= enc;
        return true;
    case DB_INPUT_ENCODER_UP: {
        bool here = m.encoders_down & enc;
        m.encoders_down &= (uint8_t)~enc;
        return here;
    }
    default:
        return true;
    }
}

bool ui_menu_input(const db_input_t *ev)
{
    m.last_input = esp_timer_get_time();
    if (!pressed_here(ev)) {
        return false;
    }
    switch (m.page) {
    case PAGE_CLOSED:
        return home_input(ev);
    case PAGE_MAIN:
        return main_input(ev);
    case PAGE_LAN:
        return lan_input(ev);
    case PAGE_SETUP_AP:
        if (ev->kind == DB_INPUT_KEY_UP && ev->index == KEY_WLAN) {
            db_net_ap_stop();
            go(PAGE_MAIN);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void ui_menu_screen(ui_screen_t *sc, const ui_state_t *st)
{
    switch (m.page) {
    case PAGE_MAIN:
        main_screen(sc, st);
        break;
    case PAGE_LAN:
        lan_screen(sc);
        break;
    case PAGE_SETUP_AP:
        ap_screen(sc);
        break;
    default:
        home_screen(sc, st);
        break;
    }
}

static void tick(void *arg)
{
    int64_t now = esp_timer_get_time();
    /* the setup WLAN stays until it is closed: someone is typing on their phone */
    if (m.page != PAGE_CLOSED && m.page != PAGE_SETUP_AP && now - m.last_input > IDLE_CLOSE_US) {
        ESP_LOGI(TAG, "menu closed after a minute without input");
        go(PAGE_CLOSED);
        m.changed();
    }
    if (m.confirm_key >= 0 && now >= m.confirm_until) {
        m.confirm_key = -1;
        m.changed();
    }
    if (m.restart_down) { /* the countdown on the restart key */
        m.changed();
    }
}

void ui_menu_init(void (*changed)(void))
{
    m.changed = changed;
    const esp_timer_create_args_t args = {.callback = tick, .name = "menu_tick"};
    esp_timer_handle_t t;
    esp_timer_create(&args, &t);
    esp_timer_start_periodic(t, 1000000);
    const esp_timer_create_args_t save = {.callback = save_brightness, .name = "brightness_save"};
    esp_timer_create(&save, &m.save_timer);
}
