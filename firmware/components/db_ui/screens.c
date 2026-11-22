#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "ui.h"

#define IDLE_CLOSE_US      60000000
#define RESET_HOLD_US      5000000
#define HOLD_SHOWN_US      1000000
#define CONFIRM_US         3000000
#define BRIGHTNESS_STEP    5
#define C_ACTIVE           0x30c060
#define C_EDIT             0x2f7cf6
#define C_DANGER           0xe5484d
#define C_WARN             0xffb020
#define C_DIM              0x9a9aa0
#define ENCODER_BRIGHTNESS 0
#define ENCODER_NAV        3

/* What a key does on the page that is shown; the builder sets it next to the tile, so they never disagree. */
typedef enum {
    K_NONE,
    K_ALL,
    K_MENU,
    K_CLOSE,
    K_LAN,
    K_AP,
    K_AP_CLOSE,
    K_RESTART,
    K_DIMMER,
    K_BRIGHTER,
    K_FIELD0, /* K_FIELD0 + field */
    K_FIELD1,
    K_FIELD2,
    K_FIELD3,
    K_DHCP,
    K_SAVE_LAN,
    K_LAN_CLOSE,
    K_SKIP,
    K_DONE,
    K_BACK,
    K_BRIGHT_STEP,
    K_ACCEPT,
    K_DENY,
    K_RESET,
    K_KEEP,
    K_LANG,
    K_WEB,
    K_DIGIT0, /* K_DIGIT0 + digit */
    K_DIGIT9 = K_DIGIT0 + 9,
    K_PIN_PAGE,
    K_SRC0, /* K_SRC0 + index in ui_model_t.sources; keep it last */
} key_act_t;

static const char *const field_names[2][4] = {{"IP", "Maske", "Gateway", "DNS"},
                                              {"IP", "Mask", "Gateway", "DNS"}};

typedef struct {
    const ui_menu_t *m;
    const ui_model_t *md;
    const ui_layout_t *l;
    ui_screen_t *sc;
    uint8_t *acts;
} page_t;

/* The German or the English text, as the deck's language is set. */
#define L(de, en) (p->md->english ? (en) : (de))

/* The long text on big keys, the short one on 72 and 96 px keys. */
static const char *fit(const page_t *p, const char *wide, const char *narrow)
{
    return p->l->key_px >= 110 ? wide : narrow;
}

static int at(const page_t *p, int row, int col)
{
    if (col < 0) {
        col += p->l->cols; /* from the right */
    }
    return row < p->l->rows && col >= 0 && col < p->l->cols ? row * p->l->cols + col : -1;
}

static ui_tile_t *put(page_t *p, int key, key_act_t act)
{
    static ui_tile_t spare; /* keys a small deck does not have */
    if (key < 0 || key >= UI_MAX_KEYS) {
        return &spare;
    }
    p->acts[key] = (uint8_t)act;
    return &p->sc->tiles[key];
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

static void segment(ui_segment_t *s, int span, const char *icon, uint32_t icon_color, const char *label,
                    const char *value, const char *sub)
{
    *s = (ui_segment_t){.span = (uint8_t)span, .icon = icon, .icon_color = icon_color, .bar = -1};
    snprintf(s->label, sizeof(s->label), "%s", label);
    snprintf(s->value, sizeof(s->value), "%s", value);
    snprintf(s->sub, sizeof(s->sub), "%s", sub);
}

static bool confirming(const ui_menu_t *m, int key, int64_t now)
{
    return m->confirm_key == key && now < m->confirm_until;
}

static const char *ip_of(const ui_model_t *md)
{
    return md->lan_up ? md->lan_ip : (md->wlan_up ? md->wlan_ip : NULL);
}

static const char *controller_label(int c)
{
    return c == UI_CTRL_SERVER ? "Server" : (c == UI_CTRL_APP ? "Elgato App" : "Companion");
}

static const char *const via_label[] = {"", "Satellite", "Dock"}; /* UI_VIA_*; a server says it once */

static const char *controller_icon(int c)
{
    return c == UI_CTRL_SERVER ? ICON_SERVER : (c == UI_CTRL_APP ? ICON_APP : ICON_COMPANION);
}

static void source_key(page_t *p, int key, int i, bool ask)
{
    const ui_source_t *src = &p->md->sources[i];
    bool asking = ask && !src->chosen && confirming(p->m, key, p->md->now_us);
    ui_tile_t *t = put(p, key, (key_act_t)(K_SRC0 + i));
    /* a card, so a name breaks into lines instead of being cut; on small keys the icon names the program */
    card(t, controller_icon(src->kind),
         asking ? L("Wechseln?", "Switch?") : fit(p, controller_label(src->kind), ""), src->label,
         asking ? fit(p, L("nochmal tippen", "tap again"), L("nochmal", "again")) : via_label[src->via]);
    t->active = src->chosen || asking;
    t->color = asking ? C_EDIT : C_ACTIVE;
}

/*
 * The top row left of the menu key: every host that can draw, the chosen one first, and "Alle" when they do
 * not fit. While the chosen program is away its key stays in amber, so a stray tap cannot hide that.
 * `ask`: a second tap confirms (the home screen).
 */
static void host_row(page_t *p, bool ask, bool welcome)
{
    const ui_model_t *md = p->md;
    int room = p->l->cols - 1;
    int order[UI_SOURCES];
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < md->source_count; i++) {
            if (md->sources[i].chosen == (pass == 0)) {
                order[n++] = i;
            }
        }
    }
    int slot = 0;
    if (!welcome && (n == 0 || !md->sources[order[0]].chosen)) {
        ui_tile_t *t = put(p, at(p, 0, slot++), K_NONE);
        card(t, controller_icon(md->controller), fit(p, controller_label(md->controller), ""),
             L("getrennt", "offline"), L("wartet", "waiting"));
        t->active = true;
        t->color = C_WARN;
    } else if (n == 0) {
        card(put(p, at(p, 0, slot++), K_NONE), ICON_ALERT,
             fit(p, L("Noch niemand", "Nobody yet"), L("Niemand", "Nobody")), L("verbunden", "connected"),
             "");
    }
    bool more = slot + n > room;
    int shown = more ? room - slot - 1 : n;
    for (int i = 0; i < shown; i++) {
        source_key(p, at(p, 0, slot++), order[i], ask && !welcome);
    }
    if (more) {
        char count[24];
        snprintf(count, sizeof(count), fit(p, L("%d Rechner", "%d computers"), L("%d Rechner", "%d")), n);
        ui_tile_t *t = put(p, at(p, 0, room - 1), K_ALL);
        button(t, ICON_OPEN, L("Alle", "All"), count);
        t->badge = BADGE_OPEN;
    }
}

static void lan_card(const page_t *p, ui_tile_t *t)
{
    const ui_model_t *md = p->md;
    card(t, ICON_LAN, "LAN",
         md->lan_up ? md->lan_ip
                    : (md->lan_link ? L("keine Adresse", "no address") : L("kein Kabel", "no cable")),
         md->lan_dhcp ? fit(p, L("automatisch", "automatic"), "auto")
                      : fit(p, L("feste Adresse", "fixed address"), L("fest", "fixed")));
}

/* The SSID can be 32 characters; it is in the web UI, the key shows the signal. */
static void wlan_card(const page_t *p, ui_tile_t *t)
{
    const ui_model_t *md = p->md;
    char rssi[16];
    snprintf(rssi, sizeof(rssi), "%d dBm", md->wlan_rssi);
    card(t, md->wlan_up ? ICON_WLAN : ICON_WLAN_OFF, L("WLAN", "Wi-Fi"),
         md->wlan_up ? md->wlan_ip : (md->wifi_ssid[0] ? L("getrennt", "offline") : L("aus", "off")),
         md->wlan_up ? rssi : "");
}

/* Encoder 1 and the strip above it on decks that have them, two keys on the others. */
static void brightness(page_t *p, int row)
{
    char v[16];
    snprintf(v, sizeof(v), "%d %%", p->md->brightness);
    if (p->l->strip && p->l->encoders) {
        segment(&p->sc->seg[0], 1, ICON_BRIGHTNESS, C_WARN, L("Helligkeit · drehen", "Brightness · turn"), v,
                "");
        p->sc->seg[0].bar = p->md->brightness;
        return;
    }
    button(put(p, at(p, row, 0), K_DIMMER), ICON_BRIGHTNESS, L("Dunkler", "Darker"), v);
    button(put(p, at(p, row, 1), K_BRIGHTER), ICON_BRIGHTNESS, L("Heller", "Brighter"), v);
}

/* Two rows and no strip (Stream Deck Neo, Stream Dock N4): no room for a row of brightness keys. */
static bool cramped(const page_t *p)
{
    return !p->l->strip && p->l->rows < 3;
}

/* One key that steps the brightness up by 20 % and starts over at 20 % after 100 %. */
static void brightness_step(page_t *p, int key)
{
    char v[16];
    snprintf(v, sizeof(v), "%d %%", p->md->brightness);
    card(put(p, key, K_BRIGHT_STEP), ICON_BRIGHTNESS, L("Licht", "Light"), v, L("tippen", "tap"));
}

/* What is missing: a wide strip part, or one card on decks without a strip. */
static void status_message(page_t *p, const char *icon, uint32_t color, const char *label, const char *title,
                           const char *sub, const char *short_title, const char *short_sub)
{
    if (p->l->strip) {
        segment(&p->sc->seg[1], 3, icon, color, label, title, sub);
        return;
    }
    /* a 3 column deck has no Web key: its status key is it */
    bool web = p->l->cols < 4;
    ui_tile_t *t = put(p, at(p, 1, -1), web ? K_WEB : K_NONE);
    card(t, icon, "Status", short_title, short_sub);
    t->badge = web ? BADGE_OPEN : BADGE_NONE;
    (void)label;
}

static void home_status(page_t *p)
{
    const ui_model_t *md = p->md;
    const char *program = controller_label(md->controller);
    char title[48];
    char sub[112];
    if (!md->lan_up && !md->wlan_up) {
        /* the setup WLAN starts by itself only at boot */
        status_message(p, ICON_NO_NETWORK, C_DANGER, L("Netzwerk", "Network"), L("Kein Netz", "No network"),
                       md->wlan_chip
                           ? L("LAN-Kabel stecken oder WLAN antippen", "Plug in a LAN cable or tap Wi-Fi")
                           : L("LAN-Kabel stecken", "Plug in a LAN cable"),
                       L("Kein Netz", "No network"), L("LAN-Kabel", "LAN cable"));
    } else if (md->hosts[md->controller] && md->source_count) { /* connected, but has not drawn yet */
        snprintf(title, sizeof(title), L("%s verbunden", "%s connected"), program);
        status_message(p, controller_icon(md->controller), C_ACTIVE, program, title,
                       L("Zeichnet gleich", "Draws in a moment"), program, L("verbunden", "connected"));
    } else if (md->controller == UI_CTRL_COMPANION && md->satellite[0]) {
        /* not reachable only once an attempt failed: right after the start it is still trying */
        bool failed = md->sat_failed;
        snprintf(sub, sizeof(sub),
                 failed ? L("%s · im Web ändern", "%s · change it on the web page") : "Satellite %s",
                 md->satellite);
        status_message(p, ICON_ALERT, C_WARN, program,
                       failed ? L("Companion nicht erreichbar", "Companion not reachable")
                              : L("Warte auf Companion", "Waiting for Companion"),
                       sub, program, failed ? L("nicht da", "away") : L("wartet", "waiting"));
    } else if (md->controller == UI_CTRL_COMPANION) {
        snprintf(sub, sizeof(sub),
                 L("In Companion unter Surfaces als Remote Surface eintragen: %s",
                   "In Companion, add %s under Surfaces as a remote surface"),
                 ip_of(md));
        status_message(p, ICON_ALERT, C_WARN, program, L("Warte auf Companion", "Waiting for Companion"), sub,
                       program, L("wartet", "waiting"));
    } else if (md->controller == UI_CTRL_APP) {
        status_message(p, ICON_ALERT, C_WARN, program,
                       L("Warte auf die Elgato App", "Waiting for the Elgato App"),
                       L("Die Elgato App findet den Deck Connector im Netz selbst",
                         "The Elgato App finds the Deck Connector on the network by itself"),
                       program, L("wartet", "waiting"));
    } else {
        snprintf(sub, sizeof(sub), "%s",
                 md->server_url[0] ? md->server_url
                                   : L("Sucht einen Server im Netz", "Looks for a server on the network"));
        status_message(p, ICON_ALERT, C_WARN, program,
                       md->server_url[0] ? L("Server getrennt", "Server offline")
                                         : L("Suche Server", "Looking for a server"),
                       sub, program, md->server_url[0] ? L("getrennt", "offline") : L("sucht", "looking"));
    }
}

static void firmware_card(const page_t *p, ui_tile_t *t)
{
    const ui_model_t *md = p->md;
    char ver[32];
    char chip[24];
    snprintf(ver, sizeof(ver), "%s", md->fw);
    char *hash = strstr(ver, "-g"); /* "v0.1.0-20-ge4fdacd-dirty": the build count is enough on a key */
    if (hash) {
        *hash = '\0';
    }
    snprintf(chip, sizeof(chip), p->l->key_px < 110 ? "%s" : L("WLAN %s", "Wi-Fi %s"),
             md->wlan_fw[0] ? md->wlan_fw : "-");
    card(t, ICON_FIRMWARE, "Firmware", ver, chip);
}

/* Nobody draws. Every key does something: LAN opens its page, Wi-Fi without a network starts the setup Wi-Fi,
 * Web shows the address and password; the strip says what is missing. */
static void home(page_t *p)
{
    const ui_model_t *md = p->md;
    host_row(p, true, false);
    ui_tile_t *menu = put(p, at(p, 0, -1), K_MENU);
    button(menu, ICON_SETTINGS, L("Menü", "Menu"), fit(p, L("Einstellungen", "settings"), ""));
    menu->badge = BADGE_OPEN;
    ui_tile_t *lan = put(p, at(p, 1, 0), K_LAN);
    lan_card(p, lan);
    lan->badge = BADGE_OPEN;
    bool join = md->wlan_chip && !md->wlan_up;
    ui_tile_t *wlan = put(p, at(p, 1, 1), join ? K_AP : K_NONE);
    wlan_card(p, wlan);
    if (join) {
        snprintf(wlan->sub, sizeof(wlan->sub), "%s", fit(p, L("einrichten", "set up"), L("Setup", "setup")));
        wlan->badge = BADGE_OPEN;
    }
    if (cramped(p)) {
        /* 3 x 2: the middle key, the setup WLAN is in the menu; the address and password are on Status */
        brightness_step(p, at(p, 1, p->l->cols < 4 ? 1 : 2));
    } else {
        ui_tile_t *web = put(p, at(p, 1, 2), K_WEB);
        button(web, ICON_WEB, "Web",
               fit(p, L("Adresse, Passwort", "address, password"), L("Login", "login")));
        web->badge = BADGE_OPEN;
    }
    brightness(p, 2);
    home_status(p);
}

static void restart_key(page_t *p, int key)
{
    const ui_menu_t *m = p->m;
    int64_t held = m->restart_down ? p->md->now_us - m->restart_down : 0;
    ui_tile_t *t = put(p, key, K_RESTART);
    char sub[24];
    if (held >= RESET_HOLD_US) {
        button(t, ICON_RESET, fit(p, L("Werkszustand", "Factory reset"), L("Löschen", "Reset")),
               L("loslassen", "let go"));
    } else if (held >= HOLD_SHOWN_US) {
        snprintf(sub, sizeof(sub), "in %d s", (int)((RESET_HOLD_US - held) / 1000000) + 1);
        button(t, ICON_RESET, fit(p, L("Werkszustand", "Factory reset"), L("Löschen", "Reset")), sub);
    } else if (confirming(m, key, p->md->now_us)) {
        button(t, ICON_RESTART, L("Wirklich?", "Sure?"),
               fit(p, L("nochmal tippen", "tap again"), L("nochmal", "again")));
    } else {
        button(t, ICON_RESTART, L("Neustart", "Restart"),
               fit(p, L("5 s: Werkszustand", "5 s: factory reset"), L("5s: löschen", "5s: reset")));
    }
    t->active = held >= HOLD_SHOWN_US || confirming(m, key, p->md->now_us);
    t->color = C_DANGER;
}

static void close_key(ui_tile_t *t, const char *icon, const char *label, const char *sub)
{
    button(t, icon, label, sub);
    t->badge = BADGE_CLOSE;
    t->color = C_EDIT;
}

/* The settings. The hosts stay on the home screen: here they would only be in the way. */
static void main_menu(page_t *p)
{
    const ui_model_t *md = p->md;
    close_key(put(p, at(p, 0, -1), K_CLOSE), ICON_SETTINGS, L("Menü", "Menu"), L("schließen", "close"));
    /* the language it switches to, named in that language, so someone who reads only that one finds it */
    button(put(p, at(p, 0, 0), K_LANG), ICON_LANG, L("English", "Deutsch"),
           fit(p, L("Sprache", "Language"), ""));
    if (cramped(p)) {
        brightness_step(p, at(p, 0, 1)); /* no row for brightness keys */
    }
    ui_tile_t *lan = put(p, at(p, 1, 0), K_LAN);
    lan_card(p, lan);
    lan->badge = BADGE_OPEN;
    ui_tile_t *ap = put(p, at(p, 1, 1), md->wlan_chip ? K_AP : K_NONE);
    button(ap, ICON_SETUP_AP, fit(p, L("Setup-WLAN", "Setup Wi-Fi"), "Setup"),
           md->wlan_chip ? fit(p, L("starten", "start"), L("WLAN", "Wi-Fi"))
                         : fit(p, L("nicht verfügbar", "not available"), L("fehlt", "none")));
    ap->badge = md->wlan_chip ? BADGE_OPEN : BADGE_NONE;
    ap->dim = !md->wlan_chip;
    restart_key(p, at(p, 1, -1));
    char web[48];
    char login[72];
    snprintf(web, sizeof(web), "%s%s", ip_of(md) ? "http://" : L("Kein Netz", "No network"),
             ip_of(md) ? ip_of(md) : "");
    snprintf(login, sizeof(login), L("Passwort %s", "Password %s"), md->web_pass);
    brightness(p, 2);
    if (p->l->strip) {
        firmware_card(p, put(p, at(p, 1, 2), K_NONE));
        segment(&p->sc->seg[1], 2, ICON_WEB, C_DIM, L("Web-Zugang", "Web access"), web,
                p->m->note[0] ? p->m->note : (md->web_pass[0] ? login : ""));
        segment(&p->sc->seg[3], 1, ICON_CLOSE, C_DIM, "", L("Schließen", "Close"),
                L("tippen oder drehen", "tap or turn"));
    } else {
        /* no third row when cramped: the address is on the LAN key beside it, so the password goes here */
        if (cramped(p)) {
            if (md->web_pass[0] && p->l->cols >= 4) { /* 3 x 2: Status on the home screen shows it */
                card(put(p, at(p, 1, 2), K_NONE), ICON_PASSWORD, L("Passwort", "Password"), md->web_pass,
                     fit(p, L("fürs Web", "for the web"), ""));
            }
        } else {
            card(put(p, at(p, 1, 2), K_NONE), ICON_WEB, "Web",
                 ip_of(md) ? ip_of(md) : L("kein Netz", "no network"), "");
            if (md->web_pass[0]) {
                card(put(p, at(p, 2, 2), K_NONE), ICON_PASSWORD, L("Passwort", "Password"), md->web_pass, "");
            }
        }
    }
}

static void octets_str(const uint8_t *o, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
}

/* With encoders every number turns; without, the deck keeps DHCP or the address it has, the web UI does more.
 */
static void lan_page(page_t *p)
{
    const ui_menu_t *m = p->m;
    bool turn = p->l->encoders >= 4 && p->l->strip;
    for (int f = 0; f < 4; f++) {
        char v[20];
        octets_str(m->octets[f], v, sizeof(v));
        if (turn) {
            ui_tile_t *t = put(p, at(p, 0, f), (key_act_t)(K_FIELD0 + f));
            button(t, ICON_IP, field_names[p->md->english][f],
                   m->lan_dhcp ? L("automatisch", "automatic") : v);
            t->active = !m->lan_dhcp && f == m->field;
            t->color = C_EDIT;
            t->dim = m->lan_dhcp;
        } else {
            card(put(p, at(p, 0, f), K_NONE), ICON_IP, field_names[p->md->english][f],
                 m->lan_dhcp ? "auto" : v, "");
        }
    }
    close_key(put(p, at(p, 1, 0), K_LAN_CLOSE), ICON_CLOSE, L("Zurück", "Back"),
              fit(p, L("ohne Speichern", "without saving"), ""));
    button(put(p, at(p, 1, 1), K_DHCP), m->lan_dhcp ? ICON_DHCP : ICON_FIXED,
           m->lan_dhcp ? fit(p, L("Automatisch", "Automatic"), "Auto") : L("Fest", "Fixed"),
           fit(p, L("tippen: wechseln", "tap to switch"), L("wechseln", "switch")));
    ui_tile_t *save = put(p, at(p, 1, -1), K_SAVE_LAN);
    button(save, ICON_SAVE, L("Speichern", "Save"),
           fit(p, L("startet neu", "restarts"), L("Neustart", "restart")));
    save->active = true;
    save->color = C_ACTIVE;
    if (!p->l->strip) {
        card(put(p, at(p, 1, 2), K_NONE), ICON_WEB, L("Ändern", "Change"), L("im Web", "on the web"), "");
        return;
    }
    if (m->lan_dhcp) {
        segment(&p->sc->seg[0], 4, ICON_LAN, C_DIM, "LAN", L("Adresse automatisch", "Automatic address"),
                m->note[0] ? m->note : L("Die Adresse kommt vom Router", "The router gives the address"));
        return;
    }
    for (int o = 0; o < 4; o++) {
        char label[32];
        char v[12];
        snprintf(label, sizeof(label), L("%s · %d. Stelle", "%s · part %d"),
                 field_names[p->md->english][m->field], o + 1);
        snprintf(v, sizeof(v), "%u", m->octets[m->field][o]);
        segment(&p->sc->seg[o], 1, NULL, 0, label, v,
                o == 0 ? L("drehen", "turn")
                       : (o == 3 ? L("drücken: nächstes Feld", "press: next field") : ""));
    }
}

static void ap_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_SETUP_AP, fit(p, L("WLAN-Name", "Wi-Fi name"), L("WLAN", "Wi-Fi")),
         md->ap_ssid, "");
    card(put(p, at(p, 0, 1), K_NONE), ICON_PASSWORD,
         fit(p, L("WLAN-Passwort", "Wi-Fi password"), L("Kennwort", "Password")), md->ap_pass, "");
    card(put(p, at(p, 0, 2), K_NONE), ICON_WEB, fit(p, L("Dann öffnen", "Then open"), L("Öffnen", "Open")),
         md->ap_ip, fit(p, L("im Browser", "in a browser"), ""));
    close_key(put(p, at(p, 1, 1), K_AP_CLOSE), ICON_SETUP_AP, L("Beenden", "Stop"),
              fit(p, L("Setup-WLAN aus", "setup Wi-Fi off"), ""));
    if (md->ap_qr.size) {
        ui_tile_t *qr = put(p, at(p, 1, 0), K_NONE);
        *qr = (ui_tile_t){.kind = TILE_QR};
        snprintf(qr->value, sizeof(qr->value), "%s", md->ap_pass);
        p->sc->qr = md->ap_qr;
    }
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_SETUP_AP, C_ACTIVE, L("Setup-WLAN", "Setup Wi-Fi"),
                L("Setup-WLAN ist an", "Setup Wi-Fi is on"),
                L("Code mit der Handykamera scannen, die Seite öffnet sich dann von selbst",
                  "Scan the code with a phone camera, and the page opens by itself"));
    }
}

/* "Drehregler 1 + 4" / "Ecken" and "2 s halten", or "aus". */
static void chord_words(const page_t *p, char *which, size_t cap, char *hold, size_t hold_cap)
{
    bool enc;
    int last;
    if (!ui_menu_chord(p->md->menu_chord, p->l, &enc, &last)) {
        snprintf(which, cap, "%s", L("aus", "off"));
        snprintf(hold, hold_cap, "%s", fit(p, L("nur im Web", "web only"), ""));
        return;
    }
    if (enc) {
        snprintf(which, cap, "%s 1 + %d", L(fit(p, "Drehregler", "Regler"), "Dials"), last + 1);
    } else { /* the first and the last key: holding all four corners works too */
        snprintf(which, cap, "%s", fit(p, L("Tasten in den Ecken", "Corner keys"), L("Ecken", "Corners")));
    }
    int ms = p->md->menu_hold_ms;
    snprintf(hold, hold_cap,
             ms % 1000 ? fit(p, L("%d,%d s halten", "hold %d.%d s"), L("%d,%d s", "%d.%d s"))
                       : L("%d s halten", "hold %d s"),
             ms / 1000, ms % 1000 / 100);
}

/* A one-time web login for the phone that scans it, and the web wizard's step while it runs. */
static void setup_keys(page_t *p)
{
    const ui_model_t *md = p->md;
    if (md->login_qr.size && at(p, 1, 2) >= 0) {
        ui_tile_t *qr = put(p, at(p, 1, 2), K_NONE);
        *qr = (ui_tile_t){.kind = TILE_QR};
        snprintf(qr->value, sizeof(qr->value), "%s", md->login_token);
        p->sc->qr = md->login_qr;
    }
    if (md->setup_step) {
        /* the page's 5 steps for the stored 1 to 6, as FROM_NUM in web/src/pages/Wizard.tsx */
        static const uint8_t page_step[] = {1, 1, 2, 2, 3, 4, 5};
        char step[8];
        snprintf(step, sizeof(step), "%u/5", page_step[md->setup_step < 7 ? md->setup_step : 6]);
        card(put(p, at(p, 1, -1), K_NONE), ICON_WEB, "Setup", step,
             fit(p, L("im Browser", "in a browser"), ""));
    }
}

/* A new deck: the address to type into Companion or the Elgato App, and how to reach the menu. The first
 * host that connects draws by itself (ui_first_source). */
static void welcome(page_t *p)
{
    const ui_model_t *md = p->md;
    host_row(p, false, true);
    for (int i = 0; i < p->l->cols - 1; i++) {
        p->sc->tiles[at(p, 0, i)].active = false; /* nothing is chosen yet */
    }
    button(put(p, at(p, 0, -1), K_SKIP), ICON_CLOSE, L("Später", "Later"),
           fit(p, L("überspringen", "skip"), ""));
    char which[32];
    char hold[24];
    chord_words(p, which, sizeof(which), hold, sizeof(hold));
    const char *ip = ip_of(md);
    setup_keys(p);
    if (p->l->strip) {
        if (ip) {
            segment(&p->sc->seg[0], 2, ICON_WEB, C_EDIT, L("Diese Adresse eintragen", "Enter this address"),
                    ip, L("in Companion oder der Elgato App", "in Companion or the Elgato App"));
        } else {
            segment(&p->sc->seg[0], 2, ICON_NO_NETWORK, C_DANGER, L("Kein Netz", "No network"),
                    L("LAN-Kabel stecken", "Plug in a LAN cable"),
                    L("nach 1 min startet das Setup-WLAN", "the setup Wi-Fi starts after 1 min"));
        }
        segment(&p->sc->seg[2], 2, ICON_SETTINGS, C_DIM, L("Menü öffnen", "Open the menu"), which, hold);
        return;
    }
    card(put(p, at(p, 1, 0), K_NONE), ip ? ICON_WEB : ICON_NO_NETWORK, fit(p, L("Adresse", "Address"), "IP"),
         ip ? ip : L("kein Netz", "no network"),
         ip ? fit(p, "in Companion/App", L("eintragen", "enter it"))
            : fit(p, L("LAN-Kabel", "LAN cable"), L("Kabel", "cable")));
    card(put(p, at(p, 1, 1), K_NONE), ICON_SETTINGS, L("Menü", "Menu"), which, hold);
}

static void welcome_net(page_t *p)
{
    const ui_model_t *md = p->md;
    lan_card(p, put(p, at(p, 0, 0), K_NONE));
    wlan_card(p, put(p, at(p, 0, 1), K_NONE));
    ui_tile_t *ap = put(p, at(p, 0, 2), md->wlan_chip ? K_AP : K_NONE);
    button(ap, ICON_SETUP_AP, fit(p, L("Setup-WLAN", "Setup Wi-Fi"), "Setup"),
           fit(p,
               md->lan_up || md->wlan_up ? L("WLAN einrichten", "set up Wi-Fi")
                                         : L("ohne Kabel", "without cable"),
               L("WLAN", "Wi-Fi")));
    ap->badge = md->wlan_chip ? BADGE_OPEN : BADGE_NONE;
    ap->dim = !md->wlan_chip;
    ui_tile_t *done = put(p, at(p, 0, -1), K_DONE);
    button(done, ICON_SAVE, L("Fertig", "Done"), fit(p, L("Setup beenden", "finish setup"), ""));
    done->active = true;
    done->color = C_ACTIVE;
    card(put(p, at(p, 1, 0), K_NONE), ICON_WEB, "Web", ip_of(md) ? ip_of(md) : L("kein Netz", "no network"),
         "");
    card(put(p, at(p, 1, 1), K_NONE), ICON_PASSWORD, L("Passwort", "Password"), md->web_pass,
         fit(p, L("fürs Web", "for the web"), ""));
    setup_keys(p);
    if (p->l->strip) {
        char sub[112];
        if (ip_of(md)) {
            snprintf(sub, sizeof(sub),
                     L("Im Browser http://%s öffnen. Mit „Fertig“ übernimmt der Rechner das Deck.",
                       "Open http://%s in a browser. Tap Done, and the computer takes the deck."),
                     ip_of(md));
        } else {
            snprintf(sub, sizeof(sub), "%s",
                     L("LAN-Kabel stecken oder Setup-WLAN starten. Dann „Fertig“ tippen.",
                       "Plug in a LAN cable or start the setup Wi-Fi. Then tap Done."));
        }
        segment(&p->sc->seg[0], 4, md->lan_up || md->wlan_up ? ICON_LAN : ICON_NO_NETWORK,
                md->lan_up || md->wlan_up ? C_ACTIVE : C_DANGER, L("Netzwerk", "Network"),
                md->lan_up || md->wlan_up ? L("Verbunden", "Connected") : L("Kein Netz", "No network"), sub);
    }
}

/* A program that is not paired wants the deck. */
static void request_page(page_t *p)
{
    const ui_model_t *md = p->md;
    bool named = md->request_name[0];
    card(put(p, at(p, 0, 0), K_NONE), ICON_ALERT,
         fit(p, L("Verbindung", "Connection"), L("Anfrage", "Request")),
         named ? md->request_name : md->request,
         named ? md->request : fit(p, L("neues Programm", "new program"), L("neu", "new")));
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, L("Annehmen", "Accept"), fit(p, L("darf verbinden", "may connect"), ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, L("Ablehnen", "Deny"), fit(p, L("bleibt draußen", "stays out"), ""));
    no->color = C_DANGER;
    if (p->l->strip) {
        char who[48];
        snprintf(who, sizeof(who), "%.24s (%s)", md->request_name, md->request);
        segment(&p->sc->seg[0], 4, ICON_ALERT, C_WARN, L("Verbindungsanfrage", "Connection request"),
                named ? who : md->request,
                L("Ein neues Programm will das Deck steuern", "A new program wants to control the deck"));
    }
}

/* A Companion announced itself and no Satellite address is set: connect over Satellite with one key. */
static void offer_page(page_t *p)
{
    const ui_model_t *md = p->md;
    const char *says = md->offer_asked ? L("möchte das Deck", "wants the deck") : L("gefunden", "found");
    card(put(p, at(p, 0, 0), K_NONE), ICON_COMPANION, fit(p, "Companion", L("Gefunden", "Found")),
         md->offer_name, fit(p, says, ""));
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, L("Verbinden", "Connect"), fit(p, L("über Satellite", "over Satellite"), ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, L("Nein", "No"),
           md->offer_asked ? "" : fit(p, L("nicht fragen", "do not ask"), ""));
    no->color = C_DANGER;
    if (p->l->strip) {
        char v[48];
        snprintf(v, sizeof(v), L("%.24s verbinden?", "Connect %.24s?"), md->offer_name);
        segment(&p->sc->seg[0], 4, ICON_COMPANION, C_EDIT,
                md->offer_asked ? L("Companion fragt", "Companion asks")
                                : L("Companion gefunden", "Companion found"),
                v, md->offer_addr);
    }
}

/* POST /api/password/show: the web login for whoever stands at the deck. */
static void password_page(page_t *p)
{
    const ui_model_t *md = p->md;
    if (md->ca_fp[0]) { /* GET /api/ca: the code to compare before trusting the file */
        char half[2][10];
        snprintf(half[0], sizeof(half[0]), "%.9s", md->ca_fp);
        snprintf(half[1], sizeof(half[1]), "%s", md->ca_fp + 10);
        card(put(p, at(p, 0, 0), K_NONE), ICON_WEB, "CA 1/2", half[0], "");
        card(put(p, at(p, 0, 1), K_NONE), ICON_WEB, "CA 2/2", half[1], "");
        close_key(put(p, at(p, 0, -1), K_NONE), ICON_CLOSE, L("Schließen", "Close"), "");
        if (p->l->strip) {
            segment(&p->sc->seg[0], 4, ICON_WEB, C_WARN, L("CA des Connectors", "The connector's CA"),
                    md->ca_fp, L("Muss gleich sein wie im Browser", "Must match the browser"));
        }
        return;
    }
    card(put(p, at(p, 0, 0), K_NONE), ICON_PASSWORD, L("Passwort", "Password"), md->web_pass, "");
    if (ip_of(md)) {
        card(put(p, at(p, 0, 1), K_NONE), ICON_WEB, L("Adresse", "Address"), ip_of(md),
             fit(p, L("im Browser", "in a browser"), ""));
    }
    close_key(put(p, at(p, 0, -1), K_NONE), ICON_CLOSE, L("Schließen", "Close"), ""); /* any key closes it */
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_PASSWORD, C_WARN, L("Web-Passwort", "Web password"), md->web_pass,
                L("Wird nach 30 s ausgeblendet", "Hides after 30 s"));
    }
}

/* POST /api/matter {"show": true} */
static void matter_page(page_t *p)
{
    const ui_model_t *md = p->md;
    ui_tile_t *qr = put(p, at(p, 0, 0), K_NONE);
    *qr = (ui_tile_t){.kind = TILE_QR};
    snprintf(qr->value, sizeof(qr->value), "%s", md->matter_code);
    p->sc->qr = md->matter_qr;
    card(put(p, at(p, 0, 1), K_NONE), ICON_SETTINGS, "Matter", md->matter_code,
         fit(p, L("in der App", "in the app"), ""));
    close_key(put(p, at(p, 0, -1), K_NONE), ICON_CLOSE, L("Schließen", "Close"), "");
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_SETTINGS, C_ACTIVE, L("Matter-Code", "Matter code"), md->matter_code,
                L("Wird nach 60 s ausgeblendet", "Hides after 60 s"));
    }
}

/* A surface host that is not paired: the admin types this code into it. */
static void pair_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_WEB, fit(p, L("Kopplung", "Pairing"), L("Code", "Code")),
         md->pair_code, fit(p, L("im Programm", "in the program"), ""));
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_WEB, C_ACTIVE, L("Kopplungscode", "Pairing code"), md->pair_code,
                L("Im Programm eingeben, das das Deck steuern soll",
                  "Enter it in the program that is to control the deck"));
    }
}

/* POST /api/password/reset: two taps on the deck itself reset every setting. */
static void reset_page(page_t *p)
{
    int key = at(p, 0, 1);
    bool asking = confirming(p->m, key, p->md->now_us);
    card(put(p, at(p, 0, 0), K_NONE), ICON_ALERT, L("Passwort", "Password"),
         fit(p, L("vergessen?", "forgotten?"), L("vergessen", "forgotten")),
         fit(p, L("alles zurücksetzen", "reset everything"), ""));
    ui_tile_t *reset = put(p, key, K_RESET);
    button(reset, ICON_RESET,
           asking ? fit(p, L("Nochmal tippen", "Tap again"), L("Nochmal", "Again"))
                  : fit(p, L("Zurücksetzen", "Reset"), "Reset"),
           fit(p, L("Werkszustand", "factory reset"), ""));
    reset->active = true;
    reset->color = C_DANGER;
    close_key(put(p, at(p, 0, -1), K_KEEP), ICON_CLOSE, L("Abbrechen", "Cancel"), "");
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_RESET, C_DANGER, L("Passwort vergessen?", "Forgot the password?"),
                L("Setzt alle Einstellungen zurück", "Resets every setting"),
                L("Zweimal „Zurücksetzen“ tippen. Danach startet das Setup.",
                  "Tap Reset twice. Setup starts after that."));
    }
}

/* POST /api/login/request: a browser asks to get in without the password. */
static void login_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_WEB, fit(p, L("Login-Anfrage", "Login request"), "Login"),
         md->login_ip, "");
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, L("Annehmen", "Accept"), fit(p, L("darf rein", "may log in"), ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, L("Ablehnen", "Deny"), fit(p, L("bleibt draußen", "stays out"), ""));
    no->color = C_DANGER;
    int col = 0;
    if (md->login_who[0]) {
        card(put(p, at(p, 1, col++), K_NONE), ICON_WEB, L("Gerät", "Device"), md->login_who, "");
    }
    if (md->login_mac[0]) {
        card(put(p, at(p, 1, col++), K_NONE), ICON_LAN, "MAC", md->login_mac, "");
    }
    if (md->login_name[0]) {
        card(put(p, at(p, 1, col), K_NONE), ICON_LAN, "Name", md->login_name, "");
    }
    if (p->l->strip) {
        char v[48];
        char sub[112];
        snprintf(v, sizeof(v), "%.15s · %.28s", md->login_ip, md->login_who);
        snprintf(sub, sizeof(sub), "%s%s%s%s", md->login_mac[0] ? "MAC " : "", md->login_mac,
                 md->login_mac[0] ? " · " : "",
                 L("nur annehmen, wenn du das bist", "accept only if this is you"));
        segment(&p->sc->seg[0], 4, ICON_WEB, C_WARN, L("Login-Anfrage", "Login request"),
                md->login_who[0] ? v : md->login_ip, sub);
    }
}

/* Every host that can draw, as many as fit beside the back key: 7 on the Neo. */
static void sources_page(page_t *p)
{
    const ui_model_t *md = p->md;
    int back = at(p, 0, -1);
    close_key(put(p, back, K_BACK), ICON_CLOSE, L("Zurück", "Back"),
              fit(p, L("ohne Wechsel", "no change"), ""));
    int key = 0;
    for (int i = 0; i < md->source_count && key < p->l->cols * p->l->rows; i++) {
        key += key == back;
        source_key(p, key++, i, false);
    }
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_OPEN, C_EDIT, L("Wer zeichnet?", "Who draws?"),
                L("Welcher Rechner?", "Which computer?"),
                L("Antippen, dann zeichnet dieser Rechner.", "Tap one, and that computer draws."));
    }
}

static void digit_key(page_t *p, int key, int digit)
{
    ui_tile_t *t = put(p, key, (key_act_t)(K_DIGIT0 + digit));
    *t = (ui_tile_t){.kind = TILE_DIGIT, .active = p->md->pin.wrong, .color = C_DANGER};
    snprintf(t->label, sizeof(t->label), "%d", digit);
}

/* The dots typed so far, "PIN" before the first, or that the last entry was wrong. */
static void pin_card(page_t *p, ui_tile_t *t, const char *label)
{
    const ui_model_t *md = p->md;
    card(t, ICON_LOCK, label, md->pin.wrong ? L("Falsch", "Wrong") : (md->pin.count ? "" : "PIN"),
         md->pin.wrong ? L("nochmal", "again") : (md->pin.count ? "" : L("eingeben", "enter")));
    t->dots = md->pin.wrong ? 0 : md->pin.count;
    t->active = md->pin.wrong;
    t->color = C_DANGER;
}

/* On a strip part, or with `left` and `right` on the edges of the Neo's narrow info bar. */
static void pin_segment(page_t *p, ui_segment_t *g, int span, const char *sub, const char *left,
                        const char *right)
{
    const db_sat_pin_t *pin = &p->md->pin;
    const char *wrong = left ? L("Falsch", "Wrong") : L("Falsche PIN", "Wrong PIN");
    const char *enter = left ? "PIN" : L("PIN eingeben", "Enter the PIN");
    segment(g, span, ICON_LOCK, pin->wrong ? C_DANGER : C_DIM, left ? left : L("Gesperrt", "Locked"),
            pin->wrong ? wrong : (pin->count ? "" : enter),
            right ? right : (pin->wrong ? L("Nochmal von vorn", "Start again") : sub));
    g->dots = pin->wrong ? 0 : pin->count;
}

static void digit_segment(page_t *p, int i, int digit)
{
    ui_segment_t *g = &p->sc->seg[i];
    *g = (ui_segment_t){.span = 1, .bar = -1, .digit = true, .icon_color = p->md->pin.wrong ? C_DANGER : 0};
    snprintf(g->value, sizeof(g->value), "%d", digit);
}

/* Companion's PIN, digits in phone order. Companion checks it: no delete and no enter, a wrong entry clears
 * itself once it is as long as the PIN. */
static void pad(page_t *p)
{
    const ui_layout_t *l = p->l;
    if ((l->rows >= 4 && l->cols >= 3) || (l->rows == 3 && l->cols >= 5)) {
        int c0 = (l->cols - 3) / 2;
        int r0 = l->rows >= 4 ? (l->rows - 4) / 2 : 0;
        for (int d = 1; d <= 9; d++) {
            digit_key(p, at(p, r0 + (d - 1) / 3, c0 + (d - 1) % 3), d);
        }
        if (l->rows >= 4) { /* 0 under 8, as on a phone */
            digit_key(p, at(p, r0 + 3, c0 + 1), 0);
        } else {
            digit_key(p, at(p, 1, c0 + 3), 0);
        }
        if (l->strip) {
            pin_segment(p, &p->sc->seg[0], 4, "", NULL, NULL);
        } else {
            pin_card(p, put(p, l->rows >= 4 ? at(p, r0 + 3, c0) : at(p, 1, c0 - 1), K_NONE),
                     L("Gesperrt", "Locked"));
        }
    } else if (l->rows == 2 && l->cols == 4) {
        for (int d = 1; d <= 8; d++) {
            digit_key(p, d - 1, d);
        }
        if (l->strip) { /* the strip's right half and encoders 3 and 4 */
            pin_segment(p, &p->sc->seg[0], 2, L("9 und 0 rechts", "9 and 0 on the right"), NULL, NULL);
            digit_segment(p, 2, 9);
            digit_segment(p, 3, 0);
        } else { /* the Neo's two touch points below the keys */
            put(p, 8, K_DIGIT9);
            put(p, 9, K_DIGIT0);
            pin_segment(p, &p->sc->bar, 1, NULL, "9", "0");
        }
    } else if (l->rows == 2 && l->cols == 5) { /* Stream Dock N4: 1 to 5 over 6 to 0 */
        for (int d = 1; d <= 10; d++) {
            digit_key(p, d - 1, d % 10);
        }
    } else if (l->rows * l->cols == 6) { /* two pages, the last key switches */
        int first = p->m->pin_page ? 6 : 1;
        for (int i = 0; i < 5; i++) {
            digit_key(p, i, (first + i) % 10);
        }
        ui_tile_t *t = put(p, 5, K_PIN_PAGE);
        pin_card(p, t, p->m->pin_page ? "1–5" : "6–0");
        t->badge = BADGE_OPEN;
    }
}

/* A deck of keys only follows Companion's rotation, as its rotateXYForPanel places the buttons; the + and the
 * Neo stay upright, their strip has one way up. */
static void lock_page(page_t *p)
{
    const ui_layout_t *l = p->l;
    int turn = p->md->pin.rotation;
    bool side = turn == 90 || turn == -90;
    /* the N4's 2 x 5 turned on its side is a 5 x 2 no pad fits: its digits stay upright */
    if (!turn || l->strip || l->bar || l->cols * l->rows > UI_MAX_KEYS ||
        (side && l->rows == 2 && l->cols == 5)) {
        pad(p);
        return;
    }
    ui_layout_t v = *l;
    v.cols = side ? l->rows : l->cols;
    v.rows = side ? l->cols : l->rows;
    static ui_screen_t up; /* callers hold the UI lock */
    uint8_t acts[UI_MAX_KEYS] = {0};
    memset(&up, 0, sizeof(up));
    page_t q = {p->m, p->md, &v, &up, acts};
    pad(&q);
    for (int i = 0; i < v.cols * v.rows; i++) {
        int x = i % v.cols;
        int y = i / v.cols;
        int px = turn == 90 ? y : (turn == -90 ? v.rows - y - 1 : v.cols - x - 1);
        int py = turn == 90 ? v.cols - x - 1 : (turn == -90 ? x : v.rows - y - 1);
        int k = py * l->cols + px;
        p->sc->tiles[k] = up.tiles[i];
        p->sc->tiles[k].turn = (int16_t)(up.tiles[i].kind ? turn : 0);
        p->acts[k] = acts[i];
    }
}

void ui_menu_lock(ui_menu_t *m, bool locked, int64_t now)
{
    if (locked && ui_menu_open(m)) {
        ui_menu_close(m, now);
    }
    if (!locked) {
        m->pin_page = 0;
    }
}

static void build(const ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc,
                  uint8_t *acts)
{
    memset(sc, 0, sizeof(*sc));
    memset(acts, 0, UI_MAX_KEYS);
    page_t p = {m, md, l, sc, acts};
    switch (m->page) {
    case PAGE_MAIN:
        main_menu(&p);
        break;
    case PAGE_LAN:
        lan_page(&p);
        break;
    case PAGE_SETUP_AP:
        ap_page(&p);
        break;
    case PAGE_WELCOME:
        welcome(&p);
        break;
    case PAGE_WELCOME_NET:
        welcome_net(&p);
        break;
    case PAGE_SOURCES:
        sources_page(&p);
        break;
    case PAGE_REQUEST:
        request_page(&p);
        break;
    case PAGE_OFFER:
        offer_page(&p);
        break;
    case PAGE_PASSWORD:
        password_page(&p);
        break;
    case PAGE_MATTER:
        matter_page(&p);
        break;
    case PAGE_RESET:
        reset_page(&p);
        break;
    case PAGE_LOGIN_REQ:
        login_page(&p);
        break;
    case PAGE_LOCK:
        lock_page(&p);
        break;
    case PAGE_PAIR:
        pair_page(&p);
        break;
    default:
        home(&p);
        break;
    }
}

/* The page that shows: a connection request comes first, then a pairing code, a reset question, a login
 * request, a Companion asking for the deck, the password, the running setup WLAN, a found Companion, then the
 * welcome steps until setup is done. */
static ui_menu_t shown_menu(const ui_menu_t *m, const ui_model_t *md)
{
    ui_menu_t shown = *m;
    if (md->request[0]) {
        shown.page = PAGE_REQUEST;
    } else if (md->pair_code[0] && (md->pair_cover || m->page == PAGE_HOME)) {
        shown.page = PAGE_PAIR;
    } else if (md->reset_ask) {
        shown.page = PAGE_RESET;
    } else if (md->login_ip[0]) {
        shown.page = PAGE_LOGIN_REQ;
    } else if (md->offer_asked) { /* NOLINT(bugprone-branch-clone): the order is the priority */
        shown.page = PAGE_OFFER;
    } else if (md->show_pass) {
        shown.page = PAGE_PASSWORD;
    } else if (md->show_matter) {
        shown.page = PAGE_MATTER;
    } else if (md->locked) {
        shown.page = PAGE_LOCK;
    } else if (m->page == PAGE_HOME && md->ap_on) { /* also when it started by itself: its password is here */
        shown.page = PAGE_SETUP_AP;
    } else if (m->page == PAGE_HOME && md->offer_addr[0]) {
        shown.page = PAGE_OFFER;
    } else if (m->page == PAGE_HOME && !md->setup_done) {
        shown.page = PAGE_WELCOME;
    }
    return shown;
}

void ui_menu_screen(const ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc)
{
    uint8_t acts[UI_MAX_KEYS];
    ui_menu_t shown = shown_menu(m, md);
    build(&shown, md, l, sc, acts);
}

void ui_update_screen(const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    bool en = md->english;
    const char *what = md->update_wlan ? (en ? "Wi-Fi chip" : "WLAN-Chip") : "Firmware";
    char v[48];
    snprintf(v, sizeof(v), "Update %s: %d %%", what, md->update_pct);
    if (l->strip) {
        segment(&sc->seg[0], 4, ICON_UPDATE, C_EDIT, "Update", v,
                en ? "Keep the power on. The Deck Connector restarts afterwards."
                   : "Nicht vom Strom trennen, der Deck Connector startet danach neu");
    }
    char pct[12];
    snprintf(pct, sizeof(pct), "%d %%", md->update_pct);
    card(&sc->tiles[0], ICON_UPDATE, "Update", pct, l->key_px >= 110 ? what : "");
    if (!l->strip) {
        card(&sc->tiles[1], ICON_ALERT, en ? "Power" : "Strom", en ? "keep on" : "nicht trennen", "");
    }
}

int ui_light(bool menu, int brightness)
{
    return menu && brightness < 20 ? 20 : brightness;
}

int ui_after_menu(int lit, int before, bool turned, int saved)
{
    int want = turned ? saved : before;
    return want >= 0 && lit >= 0 && want != lit ? want : -1;
}

bool ui_menu_open(const ui_menu_t *m)
{
    return m->page == PAGE_MAIN || m->page == PAGE_LAN || m->page == PAGE_SETUP_AP || m->page == PAGE_SOURCES;
}

static void go(ui_menu_t *m, ui_page_t page, int64_t now)
{
    m->page = page;
    m->note[0] = '\0';
    m->confirm_until = 0;
    m->confirm_key = -1;
    m->restart_down = 0;
    m->keys_down = 0; /* a press from the page before must not act here */
    m->encoders_down = 0;
    m->last_input = now;
}

void ui_menu_open_main(ui_menu_t *m, int64_t now)
{
    go(m, PAGE_MAIN, now);
}

void ui_menu_close(ui_menu_t *m, int64_t now)
{
    go(m, PAGE_HOME, now);
}

/* First tap arms, a second one within 3 s does it. */
static bool confirmed(ui_menu_t *m, int key, int64_t now)
{
    if (confirming(m, key, now)) {
        m->confirm_key = -1;
        return true;
    }
    m->confirm_key = key;
    m->confirm_until = now + CONFIRM_US;
    return false;
}

/* A release only counts when its press happened on this page: releasing the menu chord must not act. */
static bool pressed_here(ui_menu_t *m, const db_input_t *ev)
{
    uint64_t key = 1ull << (ev->index & 63);
    uint8_t enc = (uint8_t)(1u << (ev->index & 7));
    bool here = true;
    switch (ev->kind) {
    case DB_INPUT_KEY_DOWN:
        m->keys_down |= key;
        break;
    case DB_INPUT_KEY_UP:
        here = m->keys_down & key;
        m->keys_down &= ~key;
        break;
    case DB_INPUT_ENCODER_DOWN:
        m->encoders_down |= enc;
        break;
    case DB_INPUT_ENCODER_UP:
        here = m->encoders_down & enc;
        m->encoders_down &= (uint8_t)~enc;
        break;
    default:
        break;
    }
    return here;
}

static void lan_load(ui_menu_t *m, const ui_model_t *md)
{
    m->lan_dhcp = md->lan_dhcp;
    m->field = 0;
    memcpy(m->octets, md->lan_cfg, sizeof(m->octets));
}

static bool encoder_input(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, const db_input_t *ev,
                          ui_action_t *act)
{
    if (m->page == PAGE_LAN && !m->lan_dhcp && l->encoders >= 4) {
        if (ev->kind == DB_INPUT_ENCODER_TURN && ev->index < 4) {
            m->octets[m->field][ev->index] = (uint8_t)(m->octets[m->field][ev->index] + ev->value);
            return true;
        }
        if (ev->kind == DB_INPUT_ENCODER_DOWN) {
            m->field = (m->field + 1) % 4;
            return true;
        }
        return false;
    }
    bool brightness_page = m->page == PAGE_HOME || m->page == PAGE_MAIN;
    if (brightness_page && ev->kind == DB_INPUT_ENCODER_TURN && ev->index == ENCODER_BRIGHTNESS) {
        *act = (ui_action_t){.what = UI_DO_BRIGHTNESS, .value = ev->value * BRIGHTNESS_STEP};
        return true;
    }
    bool nav =
        (ev->kind == DB_INPUT_ENCODER_TURN || ev->kind == DB_INPUT_ENCODER_UP) && ev->index == ENCODER_NAV;
    if (m->page == PAGE_MAIN && (nav || (ev->kind == DB_INPUT_TOUCH_TAP && ev->x >= 600))) {
        go(m, PAGE_HOME, md->now_us);
        return true;
    }
    return false;
}

static bool restart_input(ui_menu_t *m, const ui_model_t *md, const db_input_t *ev, ui_action_t *act)
{
    if (ev->kind == DB_INPUT_KEY_DOWN) {
        m->restart_down = md->now_us;
        return true;
    }
    if (!m->restart_down) { /* pressed on another page: no hold to measure */
        return false;
    }
    int64_t held = md->now_us - m->restart_down;
    m->restart_down = 0;
    if (held >= RESET_HOLD_US) {
        act->what = UI_DO_RESET;
        snprintf(m->note, sizeof(m->note), "%s",
                 md->english ? "Factory reset, restarting" : "Werkszustand, starte neu");
    } else if (held < HOLD_SHOWN_US && confirmed(m, ev->index, md->now_us)) {
        act->what = UI_DO_RESTART;
        snprintf(m->note, sizeof(m->note), "%s", md->english ? "Restarting" : "Starte neu");
    }
    return true;
}

/* A digit on its press, so a quick typist is not held up; on the +, encoders 3 and 4 and the strip's right
 * half are 9 and 0. */
static bool lock_input(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, const db_input_t *ev,
                       ui_action_t *act)
{
    int digit = -1;
    bool plus = l->strip && l->rows == 2;
    if (ev->kind == DB_INPUT_KEY_DOWN && ev->index < UI_MAX_KEYS) {
        static ui_screen_t sc_buf; /* as in ui_menu_input */
        uint8_t acts[UI_MAX_KEYS];
        ui_menu_t shown = *m;
        shown.page = PAGE_LOCK;
        build(&shown, md, l, &sc_buf, acts);
        if (acts[ev->index] == K_PIN_PAGE) {
            m->pin_page = !m->pin_page;
            return true;
        }
        digit = acts[ev->index] >= K_DIGIT0 && acts[ev->index] <= K_DIGIT9 ? acts[ev->index] - K_DIGIT0 : -1;
    } else if (plus && ev->kind == DB_INPUT_ENCODER_DOWN && ev->index >= 2 && ev->index <= 3) {
        digit = ev->index == 2 ? 9 : 0;
    } else if (plus && ev->kind == DB_INPUT_TOUCH_TAP && ev->x >= 400) {
        digit = ev->x < 600 ? 9 : 0;
    }
    if (digit < 0) {
        return false;
    }
    *act = (ui_action_t){.what = UI_DO_PIN, .value = digit};
    return true;
}

bool ui_menu_input(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, const db_input_t *ev,
                   ui_action_t *act)
{
    *act = (ui_action_t){.what = UI_DO_NOTHING};
    m->last_input = md->now_us;
    if (!pressed_here(m, ev)) {
        return false;
    }
    ui_menu_t shown = shown_menu(m, md);
    if (shown.page == PAGE_LOCK) {
        return lock_input(m, md, l, ev, act);
    }
    bool covers = shown.page == PAGE_PASSWORD;
    covers |= shown.page == PAGE_MATTER;
    if (covers &&
        (ev->kind == DB_INPUT_KEY_UP || ev->kind == DB_INPUT_ENCODER_UP || ev->kind == DB_INPUT_TOUCH_TAP)) {
        act->what = UI_DO_HIDE_PASSWORD;
        return true;
    }
    /* a control token can press keys over the API: it must not wipe or restart the bridge, change its
     * network, open the setup WLAN, or let a browser or host in */
    bool deck_only = shown.page == PAGE_RESET || shown.page == PAGE_LOGIN_REQ;
    if (deck_only && !md->from_deck) {
        return false;
    }
    bool key = ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_KEY_UP;
    if (covers || (deck_only && !key)) {
        return false;
    }
    if (!key) {
        return encoder_input(m, md, l, ev, act);
    }
    static ui_screen_t sc_buf; /* 5 KB: too big for the deck's event task stack; one caller at a time */
    ui_screen_t *sc = &sc_buf;
    uint8_t acts[UI_MAX_KEYS];
    build(&shown, md, l, sc, acts);
    key_act_t what = ev->index < UI_MAX_KEYS ? (key_act_t)acts[ev->index] : K_NONE;
    if (!md->from_deck && (what == K_RESTART || what == K_SAVE_LAN || what == K_AP || what == K_AP_CLOSE ||
                           what == K_ACCEPT || what == K_DENY || what == K_WEB)) {
        return false;
    }
    if (what == K_RESTART) {
        return restart_input(m, md, ev, act);
    }
    if (ev->kind !=
        DB_INPUT_KEY_UP) { /* everything else acts on release, so it does not reach the next writer */
        return false;
    }
    switch (what) {
    case K_ALL:
        m->pick_back = shown.page;
        go(m, PAGE_SOURCES, md->now_us);
        return true;
    case K_MENU:
        go(m, PAGE_MAIN, md->now_us);
        return true;
    case K_BACK:
        go(m, m->pick_back, md->now_us);
        return true;
    case K_ACCEPT:
    case K_DENY:
        *act = (ui_action_t){.what = shown.page == PAGE_OFFER       ? UI_DO_OFFER
                                     : shown.page == PAGE_LOGIN_REQ ? UI_DO_LOGIN
                                                                    : UI_DO_ANSWER,
                             .value = what == K_ACCEPT};
        return true;
    case K_CLOSE:
        go(m, PAGE_HOME, md->now_us);
        return true;
    case K_RESET:
        act->what = confirmed(m, ev->index, md->now_us) ? UI_DO_RESET : UI_DO_NOTHING;
        return true;
    case K_KEEP:
        act->what = UI_DO_CANCEL_RESET;
        return true;
    case K_LAN:
        lan_load(m, md);
        m->pick_back = shown.page == PAGE_HOME ? PAGE_HOME : PAGE_MAIN;
        go(m, PAGE_LAN, md->now_us);
        return true;
    case K_LANG:
        act->what = UI_DO_LANG;
        return true;
    case K_WEB:
        act->what = UI_DO_SHOW_PASSWORD;
        return true;
    case K_AP:
        m->pick_back = shown.page == PAGE_HOME ? PAGE_HOME : PAGE_MAIN;
        act->what = UI_DO_AP_START;
        go(m, PAGE_SETUP_AP, md->now_us);
        return true;
    case K_AP_CLOSE:
        act->what = UI_DO_AP_STOP;
        go(m, m->page != PAGE_SETUP_AP ? m->page : (md->setup_done ? m->pick_back : PAGE_WELCOME_NET),
           md->now_us);
        return true;
    case K_DIMMER:
    case K_BRIGHTER:
        *act = (ui_action_t){.what = UI_DO_BRIGHTNESS, .value = what == K_DIMMER ? -10 : 10};
        return true;
    case K_BRIGHT_STEP: {
        int next = md->brightness / 20 * 20 + 20;
        *act = (ui_action_t){.what = UI_DO_BRIGHTNESS, .value = (next > 100 ? 20 : next) - md->brightness};
        return true;
    }
    case K_FIELD0:
    case K_FIELD1:
    case K_FIELD2:
    case K_FIELD3:
        m->field = (int)what - K_FIELD0;
        return true;
    case K_DHCP:
        m->lan_dhcp = !m->lan_dhcp;
        return true;
    case K_LAN_CLOSE:
        go(m, m->pick_back, md->now_us);
        return true;
    case K_SAVE_LAN:
        if (m->lan_dhcp == md->lan_dhcp &&
            (m->lan_dhcp || memcmp(m->octets, md->lan_cfg, sizeof(m->octets)) == 0)) {
            go(m, m->pick_back, md->now_us);
            snprintf(m->note, sizeof(m->note), "%s", md->english ? "LAN unchanged" : "LAN unverändert");
            return true;
        }
        *act = (ui_action_t){.what = UI_DO_SAVE_LAN, .dhcp = m->lan_dhcp};
        memcpy(act->octets, m->octets, sizeof(act->octets));
        snprintf(m->note, sizeof(m->note), "%s",
                 md->english ? "Saved, restarting" : "Gespeichert, starte neu");
        return true;
    case K_SKIP:
    case K_DONE:
        act->what = UI_DO_SETUP_DONE;
        go(m, PAGE_HOME, md->now_us);
        return true;
    default:
        if (what >= K_SRC0 && (int)what < K_SRC0 + md->source_count) {
            int i = (int)what - K_SRC0;
            /* a stray tap while the chosen host is away must not switch */
            if (shown.page == PAGE_HOME && !md->sources[i].chosen && !confirmed(m, ev->index, md->now_us)) {
                return true;
            }
            if (!md->sources[i].chosen || shown.page != PAGE_HOME) {
                *act = (ui_action_t){.what = UI_DO_SOURCE, .value = i};
            }
            ui_page_t from = shown.page == PAGE_SOURCES ? m->pick_back : shown.page;
            go(m, from == PAGE_WELCOME ? PAGE_WELCOME_NET : PAGE_HOME, md->now_us);
            return true;
        }
        return false;
    }
}

bool ui_first_source(const ui_model_t *md, ui_action_t *act)
{
    /* picked: a choice made before (a re-run setup, the web wizard) is not taken away */
    if (md->setup_done || md->picked || md->request[0] || md->offer_addr[0] || !md->source_count) {
        return false;
    }
    *act = (ui_action_t){.what = UI_DO_SOURCE, .value = 0};
    return true;
}

bool ui_menu_tick(ui_menu_t *m, const ui_model_t *md)
{
    bool redraw = false;
    /* the setup WLAN stays until it is closed: someone is typing on their phone */
    if (ui_menu_open(m) && m->page != PAGE_SETUP_AP && md->now_us - m->last_input > IDLE_CLOSE_US) {
        go(m, PAGE_HOME, md->now_us);
        redraw = true;
    }
    if (m->confirm_key >= 0 && md->now_us >= m->confirm_until) {
        m->confirm_key = -1;
        redraw = true;
    }
    return redraw || m->restart_down; /* the countdown on the restart key */
}

static int held_last(int n, uint64_t tap)
{
    for (int i = n - 1; i >= 1 && !(tap & 1); i--) {
        if (!(tap >> i & 1)) {
            return i;
        }
    }
    return -1;
}

bool ui_menu_chord(const char *chord, const ui_layout_t *l, bool *on_encoders, int *last)
{
    if (strcmp(chord, "off") == 0) {
        return false;
    }
    /* encoders: auto's choice when there are some */
    int enc = strcmp(chord, "keys") != 0 ? held_last(l->encoders, l->tap_encoders) : -1;
    *on_encoders = enc >= 1;
    *last = *on_encoders ? enc : held_last(l->cols * l->rows, l->tap_keys);
    return *last >= 1;
}
