#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_net.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

#define IDLE_CLOSE_US 60000000
#define RESET_HOLD_US 5000000
#define C_MENU        0x1d3557
#define C_KEY         0x2b3a4a
#define C_SELECTED    0x3a6ea5
#define C_DANGER      0x7a2020

static const char *TAG = "menu";

typedef enum { PAGE_CLOSED, PAGE_MAIN, PAGE_INFO, PAGE_LAN, PAGE_SETUP_AP } page_t;

static const char *const field_names[] = {"IP", "Maske", "Gateway", "DNS"};

static struct {
    void (*changed)(void);
    page_t page;
    int64_t last_input;
    int64_t restart_down;
    char note[64];
    /* LAN editor */
    bool lan_dhcp;
    int field;
    uint8_t octets[4][4];
} m;

bool ui_menu_open(void)
{
    return m.page != PAGE_CLOSED;
}

static void go(page_t p)
{
    m.page = p;
    m.note[0] = '\0';
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

/* ---------- LAN editor ---------- */

static char *field_str(db_ip_config_t *c, int i)
{
    char *fields[] = {c->ip, c->mask, c->gateway, c->dns};
    return fields[i];
}

static void lan_load(void)
{
    db_ip_config_t c = db_config()->lan;
    m.lan_dhcp = c.dhcp;
    m.field = 0;
    db_net_status_t n;
    db_net_status(&n);
    if (c.ip[0] == '\0' && n.lan_ip[0]) {
        snprintf(c.ip, sizeof(c.ip), "%s", n.lan_ip); /* start from the address DHCP gave us */
    }
    if (c.mask[0] == '\0') {
        snprintf(c.mask, sizeof(c.mask), "255.255.255.0");
    }
    for (int f = 0; f < 4; f++) {
        ip4_addr_t a = {0};
        ip4addr_aton(field_str(&c, f), &a);
        for (int o = 0; o < 4; o++) {
            m.octets[f][o] = (uint8_t)(ip4_addr_get_u32(&a) >> (8 * o));
        }
    }
}

static void lan_save(void)
{
    db_config_t c;
    db_config_copy(&c);
    c.lan.dhcp = m.lan_dhcp;
    for (int f = 0; f < 4; f++) {
        uint8_t *o = m.octets[f];
        snprintf(field_str(&c.lan, f), 16, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
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
    if (ev->kind == DB_INPUT_ENCODER_DOWN) {
        m.field = (m.field + 1) % 4;
        return true;
    }
    if (ev->kind != DB_INPUT_KEY_DOWN) {
        return false;
    }
    switch (ev->index) {
    case 0:
        m.lan_dhcp = !m.lan_dhcp;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
        m.field = ev->index - 1;
        break;
    case 6:
        lan_save();
        break;
    case 7:
        go(PAGE_MAIN);
        break;
    default:
        return false;
    }
    return true;
}

/* ---------- main page ---------- */

static void brightness(int delta)
{
    db_config_t c;
    db_config_copy(&c);
    int b = c.brightness + delta;
    c.brightness = (uint8_t)(b < 0 ? 0 : (b > 100 ? 100 : b));
    db_config_save(&c);
    db_deck_set_brightness(c.brightness);
    snprintf(m.note, sizeof(m.note), "Helligkeit %u %%", c.brightness);
}

static void toggle_mode(void)
{
    db_config_t c;
    db_config_copy(&c);
    c.mode = c.mode == DB_MODE_DOCK ? DB_MODE_NATIVE : DB_MODE_DOCK;
    if (db_config_save(&c) == ESP_OK) {
        snprintf(m.note, sizeof(m.note), "%s, starte neu",
                 c.mode == DB_MODE_DOCK ? "Dock-Modus" : "Nativer Modus");
        restart_soon();
    }
}

static bool main_key_up(uint8_t key)
{
    if (key != 6 || !m.restart_down) {
        return false;
    }
    bool reset = esp_timer_get_time() - m.restart_down >= RESET_HOLD_US;
    m.restart_down = 0;
    if (reset) {
        ESP_LOGW(TAG, "factory reset from the menu");
        db_config_reset();
        snprintf(m.note, sizeof(m.note), "Werkseinstellungen, starte neu");
    } else {
        snprintf(m.note, sizeof(m.note), "Starte neu");
    }
    restart_soon();
    return true;
}

static bool main_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_KEY_UP) {
        return main_key_up(ev->index);
    }
    if (ev->kind != DB_INPUT_KEY_DOWN) {
        return false;
    }
    switch (ev->index) {
    case 0:
        go(PAGE_INFO);
        break;
    case 1:
        lan_load();
        go(PAGE_LAN);
        break;
    case 2:
        if (db_net_ap_start() == ESP_OK) {
            go(PAGE_SETUP_AP);
        } else {
            snprintf(m.note, sizeof(m.note), "Kein WLAN-Chip");
        }
        break;
    case 3:
        toggle_mode();
        break;
    case 4:
        brightness(-10);
        break;
    case 5:
        brightness(10);
        break;
    case 6:
        m.restart_down = esp_timer_get_time();
        snprintf(m.note, sizeof(m.note), "Loslassen: Neustart, 5 s halten: Werkseinstellungen");
        break;
    case 7:
        go(PAGE_CLOSED);
        break;
    default:
        return false;
    }
    return true;
}

bool ui_menu_input(const db_input_t *ev)
{
    m.last_input = esp_timer_get_time();
    switch (m.page) {
    case PAGE_MAIN:
        return main_input(ev);
    case PAGE_LAN:
        return lan_input(ev);
    case PAGE_INFO:
    case PAGE_SETUP_AP:
        if (ev->kind == DB_INPUT_KEY_DOWN && ev->index == 7) {
            if (m.page == PAGE_SETUP_AP) {
                db_net_ap_stop();
            }
            go(PAGE_MAIN);
            return true;
        }
        return false;
    default:
        return false;
    }
}

/* ---------- screens ---------- */

static void main_screen(ui_screen_t *sc)
{
    const db_config_t *c = db_config();
    char b[16];
    snprintf(b, sizeof(b), "%u %%", c->brightness);
    snprintf(sc->title, sizeof(sc->title), "Einstellungen");
    snprintf(sc->line, sizeof(sc->line), "%s", m.note[0] ? m.note : "Taste wählen, Zurück schließt das Menü");
    ui_set_tile(sc, 0, "Info", "Adressen, Passwort", C_KEY);
    ui_set_tile(sc, 1, "LAN-IP", c->lan.dhcp ? "DHCP" : c->lan.ip, C_KEY);
    ui_set_tile(sc, 2, "Setup-WLAN", "zum Einrichten", C_KEY);
    ui_set_tile(sc, 3, "Modus", c->mode == DB_MODE_DOCK ? "Dock → Nativ" : "Nativ → Dock", C_KEY);
    ui_set_tile(sc, 4, "Heller −", b, C_KEY);
    ui_set_tile(sc, 5, "Heller +", b, C_KEY);
    ui_set_tile(sc, 6, "Neustart", "5 s: Werkseinst.", C_DANGER);
    ui_set_tile(sc, 7, "Zurück", "", C_SELECTED);
}

static void info_screen(ui_screen_t *sc)
{
    const db_config_t *c = db_config();
    db_net_status_t n;
    db_net_status(&n);
    snprintf(sc->title, sizeof(sc->title), "%s", c->name);
    snprintf(sc->line, sizeof(sc->line), "Web: http://%s  ·  Benutzer admin  ·  Passwort %s",
             n.lan_up ? n.lan_ip : (n.wlan_up ? n.wlan_ip : "-"), c->admin_pass);
    ui_set_tile(sc, 0, "LAN", n.lan_up ? n.lan_ip : "-", C_KEY);
    ui_set_tile(sc, 1, "WLAN", n.wlan_up ? n.wlan_ip : "-", C_KEY);
    ui_set_tile(sc, 2, "Server 1", c->servers[0][0] ? c->servers[0] + 5 : "mDNS", C_KEY);
    ui_set_tile(sc, 3, "Server 2", c->servers[1][0] ? c->servers[1] + 5 : "-", C_KEY);
    ui_set_tile(sc, 4, "Passwort", c->admin_pass, C_KEY);
    ui_set_tile(sc, 5, "Firmware", esp_app_get_description()->version, C_KEY);
    ui_set_tile(sc, 6, "", "", C_KEY);
    ui_set_tile(sc, 7, "Zurück", "", C_SELECTED);
}

static void lan_screen(ui_screen_t *sc)
{
    uint8_t *o = m.octets[m.field];
    if (m.lan_dhcp) {
        snprintf(sc->title, sizeof(sc->title), "LAN: DHCP");
        snprintf(sc->line, sizeof(sc->line), "%s", m.note[0] ? m.note : "Taste 1 schaltet auf feste Adresse");
    } else {
        snprintf(sc->title, sizeof(sc->title), "%s %u.%u.%u.%u", field_names[m.field], o[0], o[1], o[2],
                 o[3]);
        snprintf(sc->line, sizeof(sc->line), "%s",
                 m.note[0] ? m.note : "Regler 1 bis 4 drehen: Zahlen · Regler drücken: nächstes Feld");
    }
    ui_set_tile(sc, 0, m.lan_dhcp ? "DHCP" : "Fest", "umschalten", C_KEY);
    for (int f = 0; f < 4; f++) {
        char v[20];
        snprintf(v, sizeof(v), "%u.%u.%u.%u", m.octets[f][0], m.octets[f][1], m.octets[f][2], m.octets[f][3]);
        ui_set_tile(sc, 1 + f, field_names[f], m.lan_dhcp ? "-" : v,
                    f == m.field && !m.lan_dhcp ? C_SELECTED : C_KEY);
    }
    ui_set_tile(sc, 5, "", "", C_KEY);
    ui_set_tile(sc, 6, "Speichern", "und neu starten", 0x1f7a45);
    ui_set_tile(sc, 7, "Zurück", "verwerfen", C_KEY);
}

static void ap_screen(ui_screen_t *sc)
{
    db_net_status_t n;
    db_net_status(&n);
    snprintf(sc->title, sizeof(sc->title), "WLAN %s", n.ap_ssid);
    snprintf(sc->line, sizeof(sc->line), "Passwort %s  ·  dann http://%s öffnen", n.ap_pass, n.ap_ip);
    ui_set_tile(sc, 0, "WLAN", n.ap_ssid, C_KEY);
    ui_set_tile(sc, 1, "Passwort", n.ap_pass, C_KEY);
    ui_set_tile(sc, 2, "Adresse", n.ap_ip, C_KEY);
    ui_set_tile(sc, 3, "Web-Login", db_config()->admin_pass, C_KEY);
    ui_set_tile(sc, 7, "Beenden", "Setup-WLAN aus", C_SELECTED);
}

void ui_menu_screen(ui_screen_t *sc)
{
    sc->color = C_MENU;
    switch (m.page) {
    case PAGE_INFO:
        info_screen(sc);
        break;
    case PAGE_LAN:
        lan_screen(sc);
        break;
    case PAGE_SETUP_AP:
        ap_screen(sc);
        break;
    default:
        main_screen(sc);
        break;
    }
}

static void idle_check(void *arg)
{
    /* The setup AP stays until it is closed: someone is typing on their phone. */
    if (m.page != PAGE_CLOSED && m.page != PAGE_SETUP_AP &&
        esp_timer_get_time() - m.last_input > IDLE_CLOSE_US) {
        ESP_LOGI(TAG, "menu closed after a minute without input");
        go(PAGE_CLOSED);
        m.changed();
    }
}

void ui_menu_init(void (*changed)(void))
{
    m.changed = changed;
    const esp_timer_create_args_t args = {.callback = idle_check, .name = "menu_idle"};
    esp_timer_handle_t t;
    esp_timer_create(&args, &t);
    esp_timer_start_periodic(t, 1000000);
}
