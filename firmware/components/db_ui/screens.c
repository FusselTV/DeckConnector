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
    K_SRC0, /* K_SRC0 + index in ui_model_t.sources; keep it last */
} key_act_t;

static const char *const field_names[] = {"IP", "Maske", "Gateway", "DNS"};

typedef struct {
    const ui_menu_t *m;
    const ui_model_t *md;
    const ui_layout_t *l;
    ui_screen_t *sc;
    uint8_t *acts;
} page_t;

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
    card(t, controller_icon(src->kind), asking ? "Wechseln?" : fit(p, controller_label(src->kind), ""),
         src->label, asking ? fit(p, "nochmal tippen", "nochmal") : via_label[src->via]);
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
        card(t, controller_icon(md->controller), controller_label(md->controller), "getrennt", "wartet");
        t->active = true;
        t->color = C_WARN;
    } else if (n == 0) {
        card(put(p, at(p, 0, slot++), K_NONE), ICON_ALERT, fit(p, "Noch niemand", "Niemand"), "verbunden",
             "");
    }
    bool more = slot + n > room;
    int shown = more ? room - slot - 1 : n;
    for (int i = 0; i < shown; i++) {
        source_key(p, at(p, 0, slot++), order[i], ask && !welcome);
    }
    if (more) {
        char count[24];
        snprintf(count, sizeof(count), "%d Rechner", n);
        ui_tile_t *t = put(p, at(p, 0, room - 1), K_ALL);
        button(t, ICON_OPEN, "Alle", count);
        t->badge = BADGE_OPEN;
    }
}

static void lan_card(const page_t *p, ui_tile_t *t)
{
    const ui_model_t *md = p->md;
    card(t, ICON_LAN, "LAN", md->lan_up ? md->lan_ip : (md->lan_link ? "keine Adresse" : "kein Kabel"),
         md->lan_dhcp ? fit(p, "automatisch", "auto") : fit(p, "feste Adresse", "fest"));
}

/* The SSID can be 32 characters; it is in the web UI, the key shows the signal. */
static void wlan_card(const page_t *p, ui_tile_t *t)
{
    const ui_model_t *md = p->md;
    char rssi[16];
    snprintf(rssi, sizeof(rssi), "%d dBm", md->wlan_rssi);
    card(t, md->wlan_up ? ICON_WLAN : ICON_WLAN_OFF, "WLAN",
         md->wlan_up ? md->wlan_ip : (md->wifi_ssid[0] ? "getrennt" : "aus"), md->wlan_up ? rssi : "");
}

/* Encoder 1 and the strip above it on decks that have them, two keys on the others. */
static void brightness(page_t *p, int row)
{
    char v[16];
    snprintf(v, sizeof(v), "%d %%", p->md->brightness);
    if (p->l->strip && p->l->encoders) {
        segment(&p->sc->seg[0], 1, ICON_BRIGHTNESS, C_WARN, "Helligkeit · drehen", v, "");
        p->sc->seg[0].bar = p->md->brightness;
        return;
    }
    button(put(p, at(p, row, 0), K_DIMMER), ICON_BRIGHTNESS, "Dunkler", v);
    button(put(p, at(p, row, 1), K_BRIGHTER), ICON_BRIGHTNESS, "Heller", v);
}

/* Two rows, no strip and no encoders (Stream Deck Neo): no room for a row of brightness keys. */
static bool cramped(const page_t *p)
{
    return !p->l->strip && !p->l->encoders && p->l->rows < 3;
}

/* One key that steps the brightness up by 20 % and starts over at 20 % after 100 %. */
static void brightness_step(page_t *p, int key)
{
    char v[16];
    snprintf(v, sizeof(v), "%d %%", p->md->brightness);
    card(put(p, key, K_BRIGHT_STEP), ICON_BRIGHTNESS, "Licht", v, "tippen");
}

/* What is missing: a wide strip part, or one card on decks without a strip. */
static void status_message(page_t *p, const char *icon, uint32_t color, const char *label, const char *title,
                           const char *sub, const char *short_title, const char *short_sub)
{
    if (p->l->strip) {
        segment(&p->sc->seg[1], 3, icon, color, label, title, sub);
        return;
    }
    card(put(p, at(p, 1, -1), K_NONE), icon, "Status", short_title, short_sub);
    (void)label;
}

static void home_status(page_t *p)
{
    const ui_model_t *md = p->md;
    char sub[112];
    if (!md->lan_up && !md->wlan_up) {
        /* the setup WLAN starts by itself only at boot */
        status_message(p, ICON_NO_NETWORK, C_DANGER, "Netzwerk", "Kein Netz",
                       md->wlan_chip ? "LAN-Kabel stecken oder im Menü das Setup-WLAN starten"
                                     : "LAN-Kabel stecken",
                       "Kein Netz", "LAN-Kabel");
    } else if (md->hosts[md->controller] && md->source_count) { /* connected, but has not drawn yet */
        snprintf(sub, sizeof(sub), "%s verbunden", controller_label(md->controller));
        status_message(p, controller_icon(md->controller), C_ACTIVE, controller_label(md->controller), sub,
                       "Zeichnet gleich", controller_label(md->controller), "verbunden");
    } else if (md->controller == UI_CTRL_COMPANION) {
        snprintf(sub, sizeof(sub), "In Companion unter Surfaces als Remote Surface eintragen: %s", ip_of(md));
        status_message(p, ICON_ALERT, C_WARN, "Companion", "Warte auf Companion", sub, "Companion", "wartet");
    } else if (md->controller == UI_CTRL_APP) {
        status_message(p, ICON_ALERT, C_WARN, "Elgato App", "Warte auf die Elgato App",
                       "Die Elgato App findet den DeckConnector im Netz selbst", "Elgato App", "wartet");
    } else {
        snprintf(sub, sizeof(sub), "%s", md->server_url[0] ? md->server_url : "Sucht einen Server im Netz");
        status_message(p, ICON_ALERT, C_WARN, "Server",
                       md->server_url[0] ? "Server getrennt" : "Suche Server", sub, "Server",
                       md->server_url[0] ? "getrennt" : "sucht");
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
    snprintf(chip, sizeof(chip), p->l->key_px < 110 ? "%s" : "WLAN %s",
             md->wlan_fw[0] ? md->wlan_fw : "-");
    card(t, ICON_FIRMWARE, "Firmware", ver, chip);
}

static void home(page_t *p)
{
    host_row(p, true, false);
    ui_tile_t *menu = put(p, at(p, 0, -1), K_MENU);
    button(menu, ICON_SETTINGS, "Menü", fit(p, "Einstellungen", ""));
    menu->badge = BADGE_OPEN;
    lan_card(p, put(p, at(p, 1, 0), K_NONE));
    wlan_card(p, put(p, at(p, 1, 1), K_NONE));
    if (cramped(p)) {
        brightness_step(p, at(p, 1, 2)); /* the firmware version is in the web UI */
    } else {
        firmware_card(p, put(p, at(p, 1, 2), K_NONE));
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
        button(t, ICON_RESET, fit(p, "Werkszustand", "Löschen"), "loslassen");
    } else if (held >= HOLD_SHOWN_US) {
        snprintf(sub, sizeof(sub), "in %d s", (int)((RESET_HOLD_US - held) / 1000000) + 1);
        button(t, ICON_RESET, fit(p, "Werkszustand", "Löschen"), sub);
    } else if (confirming(m, key, p->md->now_us)) {
        button(t, ICON_RESTART, "Wirklich?", fit(p, "nochmal tippen", "nochmal"));
    } else {
        button(t, ICON_RESTART, "Neustart", fit(p, "5 s: Werkszustand", "5s: löschen"));
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

static void main_menu(page_t *p)
{
    const ui_model_t *md = p->md;
    host_row(p, false, false);
    close_key(put(p, at(p, 0, -1), K_CLOSE), ICON_SETTINGS, "Menü", "schließen");
    ui_tile_t *lan = put(p, at(p, 1, 0), K_LAN);
    lan_card(p, lan);
    lan->badge = BADGE_OPEN;
    ui_tile_t *ap = put(p, at(p, 1, 1), md->wlan_chip ? K_AP : K_NONE);
    button(ap, ICON_SETUP_AP, fit(p, "Setup-WLAN", "Setup"),
           md->wlan_chip ? fit(p, "starten", "WLAN") : fit(p, "nicht verfügbar", "fehlt"));
    ap->badge = md->wlan_chip ? BADGE_OPEN : BADGE_NONE;
    ap->dim = !md->wlan_chip;
    restart_key(p, at(p, 1, -1));
    char web[48];
    char login[72];
    snprintf(web, sizeof(web), "%s%s", ip_of(md) ? "http://" : "Kein Netz", ip_of(md) ? ip_of(md) : "");
    snprintf(login, sizeof(login), "Benutzer admin · Passwort %s", md->web_pass);
    brightness(p, 2);
    if (p->l->strip) {
        segment(&p->sc->seg[1], 2, ICON_WEB, C_DIM, "Web-Zugang", web,
                p->m->note[0] ? p->m->note : (md->web_pass[0] ? login : ""));
        segment(&p->sc->seg[3], 1, ICON_CLOSE, C_DIM, "", "Schließen", "tippen oder drehen");
    } else {
        /* no third row when cramped: the address is on the LAN key beside it, so the password goes here */
        if (cramped(p)) {
            if (md->web_pass[0]) {
                card(put(p, at(p, 1, 2), K_NONE), ICON_PASSWORD, "Passwort", md->web_pass, "admin");
            }
        } else {
            card(put(p, at(p, 1, 2), K_NONE), ICON_WEB, "Web", ip_of(md) ? ip_of(md) : "kein Netz", "admin");
            if (md->web_pass[0]) {
                card(put(p, at(p, 2, 2), K_NONE), ICON_PASSWORD, "Passwort", md->web_pass, "");
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
            button(t, ICON_IP, field_names[f], m->lan_dhcp ? "automatisch" : v);
            t->active = !m->lan_dhcp && f == m->field;
            t->color = C_EDIT;
            t->dim = m->lan_dhcp;
        } else {
            card(put(p, at(p, 0, f), K_NONE), ICON_IP, field_names[f], m->lan_dhcp ? "auto" : v, "");
        }
    }
    close_key(put(p, at(p, 1, 0), K_LAN_CLOSE), ICON_CLOSE, "Zurück", fit(p, "ohne Speichern", ""));
    button(put(p, at(p, 1, 1), K_DHCP), m->lan_dhcp ? ICON_DHCP : ICON_FIXED,
           m->lan_dhcp ? fit(p, "Automatisch", "Auto") : "Fest", fit(p, "tippen: wechseln", "wechseln"));
    ui_tile_t *save = put(p, at(p, 1, -1), K_SAVE_LAN);
    button(save, ICON_SAVE, "Speichern", fit(p, "startet neu", "Neustart"));
    save->active = true;
    save->color = C_ACTIVE;
    if (!p->l->strip) {
        card(put(p, at(p, 1, 2), K_NONE), ICON_WEB, "Ändern", "im Web", "");
        return;
    }
    if (m->lan_dhcp) {
        segment(&p->sc->seg[0], 4, ICON_LAN, C_DIM, "LAN", "Adresse automatisch",
                m->note[0] ? m->note : "Die Adresse kommt vom Router");
        return;
    }
    for (int o = 0; o < 4; o++) {
        char label[32];
        char v[12];
        snprintf(label, sizeof(label), "%s · %d. Stelle", field_names[m->field], o + 1);
        snprintf(v, sizeof(v), "%u", m->octets[m->field][o]);
        segment(&p->sc->seg[o], 1, NULL, 0, label, v,
                o == 0 ? "drehen" : (o == 3 ? "drücken: nächstes Feld" : ""));
    }
}

static void ap_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_SETUP_AP, fit(p, "WLAN-Name", "WLAN"), md->ap_ssid, "");
    card(put(p, at(p, 0, 1), K_NONE), ICON_PASSWORD, fit(p, "WLAN-Passwort", "Kennwort"), md->ap_pass, "");
    card(put(p, at(p, 0, 2), K_NONE), ICON_WEB, fit(p, "Dann öffnen", "Öffnen"), md->ap_ip,
         fit(p, "im Browser", ""));
    card(put(p, at(p, 0, 3), K_NONE), ICON_PASSWORD, fit(p, "Web-Login", "Login"), md->web_pass,
         fit(p, "Benutzer admin", "admin"));
    close_key(put(p, at(p, 1, 1), K_AP_CLOSE), ICON_SETUP_AP, "Beenden", fit(p, "Setup-WLAN aus", ""));
    if (md->ap_qr.size) {
        ui_tile_t *qr = put(p, at(p, 1, 0), K_NONE);
        *qr = (ui_tile_t){.kind = TILE_QR};
        snprintf(qr->value, sizeof(qr->value), "%s", md->ap_pass);
        p->sc->qr = md->ap_qr;
    }
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_SETUP_AP, C_ACTIVE, "Setup-WLAN", "Setup-WLAN ist an",
                "Code mit der Handykamera scannen oder von Hand verbinden und die Adresse öffnen");
    }
}

/* "Drehregler 1 + 4" / "Ecken" and "2 s halten", or "aus". */
static void chord_words(const page_t *p, char *which, size_t cap, char *hold, size_t hold_cap)
{
    bool enc;
    int last;
    if (!ui_menu_chord(p->md->menu_chord, p->l->encoders, p->l->cols * p->l->rows, &enc, &last)) {
        snprintf(which, cap, "aus");
        snprintf(hold, hold_cap, "%s", fit(p, "nur im Web", ""));
        return;
    }
    if (enc) {
        snprintf(which, cap, "%s 1 + %d", fit(p, "Drehregler", "Regler"), last + 1);
    } else { /* the first and the last key: holding all four corners works too */
        snprintf(which, cap, "%s", fit(p, "Tasten in den Ecken", "Ecken"));
    }
    int ms = p->md->menu_hold_ms;
    snprintf(hold, hold_cap, ms % 1000 ? fit(p, "%d,%d s halten", "%d,%d s") : "%d s halten", ms / 1000,
             ms % 1000 / 100);
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
        card(put(p, at(p, 1, -1), K_NONE), ICON_WEB, "Setup", step, fit(p, "im Browser", ""));
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
    button(put(p, at(p, 0, -1), K_SKIP), ICON_CLOSE, "Später", fit(p, "überspringen", ""));
    char which[32];
    char hold[24];
    chord_words(p, which, sizeof(which), hold, sizeof(hold));
    const char *ip = ip_of(md);
    setup_keys(p);
    if (p->l->strip) {
        if (ip) {
            segment(&p->sc->seg[0], 2, ICON_WEB, C_EDIT, "Diese Adresse eintragen", ip,
                    "in Companion oder der Elgato App");
        } else {
            segment(&p->sc->seg[0], 2, ICON_NO_NETWORK, C_DANGER, "Kein Netz", "LAN-Kabel stecken",
                    "nach 1 min startet das Setup-WLAN");
        }
        segment(&p->sc->seg[2], 2, ICON_SETTINGS, C_DIM, "Menü öffnen", which, hold);
        return;
    }
    card(put(p, at(p, 1, 0), K_NONE), ip ? ICON_WEB : ICON_NO_NETWORK, fit(p, "Adresse", "IP"),
         ip ? ip : "kein Netz", ip ? fit(p, "in Companion/App", "eintragen") : fit(p, "LAN-Kabel", "Kabel"));
    card(put(p, at(p, 1, 1), K_NONE), ICON_SETTINGS, "Menü", which, hold);
}

static void welcome_net(page_t *p)
{
    const ui_model_t *md = p->md;
    lan_card(p, put(p, at(p, 0, 0), K_NONE));
    wlan_card(p, put(p, at(p, 0, 1), K_NONE));
    ui_tile_t *ap = put(p, at(p, 0, 2), md->wlan_chip ? K_AP : K_NONE);
    button(ap, ICON_SETUP_AP, fit(p, "Setup-WLAN", "Setup"),
           fit(p, md->lan_up || md->wlan_up ? "WLAN einrichten" : "ohne Kabel", "WLAN"));
    ap->badge = md->wlan_chip ? BADGE_OPEN : BADGE_NONE;
    ap->dim = !md->wlan_chip;
    ui_tile_t *done = put(p, at(p, 0, -1), K_DONE);
    button(done, ICON_SAVE, "Fertig", fit(p, "Setup beenden", ""));
    done->active = true;
    done->color = C_ACTIVE;
    card(put(p, at(p, 1, 0), K_NONE), ICON_WEB, "Web", ip_of(md) ? ip_of(md) : "kein Netz",
         fit(p, "Benutzer admin", "admin"));
    card(put(p, at(p, 1, 1), K_NONE), ICON_PASSWORD, "Passwort", md->web_pass, fit(p, "fürs Web", ""));
    setup_keys(p);
    if (p->l->strip) {
        char sub[112];
        if (ip_of(md)) {
            snprintf(sub, sizeof(sub),
                     "Im Browser http://%s öffnen. Mit „Fertig“ übernimmt der Rechner das Deck.", ip_of(md));
        } else {
            snprintf(sub, sizeof(sub), "LAN-Kabel stecken oder Setup-WLAN starten. Dann „Fertig“ tippen.");
        }
        segment(&p->sc->seg[0], 4, md->lan_up || md->wlan_up ? ICON_LAN : ICON_NO_NETWORK,
                md->lan_up || md->wlan_up ? C_ACTIVE : C_DANGER, "Netzwerk",
                md->lan_up || md->wlan_up ? "Verbunden" : "Kein Netz", sub);
    }
}

/* A program that is not paired wants the deck. */
static void request_page(page_t *p)
{
    const ui_model_t *md = p->md;
    bool named = md->request_name[0];
    card(put(p, at(p, 0, 0), K_NONE), ICON_ALERT, fit(p, "Verbindung", "Anfrage"),
         named ? md->request_name : md->request, named ? md->request : fit(p, "neues Programm", "neu"));
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, "Annehmen", fit(p, "darf verbinden", ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, "Ablehnen", fit(p, "bleibt draußen", ""));
    no->color = C_DANGER;
    if (p->l->strip) {
        char who[48];
        snprintf(who, sizeof(who), "%.24s (%s)", md->request_name, md->request);
        segment(&p->sc->seg[0], 4, ICON_ALERT, C_WARN, "Verbindungsanfrage", named ? who : md->request,
                "Ein neues Programm will das Deck steuern");
    }
}

/* A Companion announced itself and no Satellite address is set: connect over Satellite with one key. */
static void offer_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_COMPANION, fit(p, "Companion", "Gefunden"), md->offer_name,
         fit(p, "gefunden", ""));
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, "Verbinden", fit(p, "über Satellite", ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, "Nein", fit(p, "nicht fragen", ""));
    no->color = C_DANGER;
    if (p->l->strip) {
        char v[48];
        snprintf(v, sizeof(v), "%.24s verbinden?", md->offer_name);
        segment(&p->sc->seg[0], 4, ICON_COMPANION, C_EDIT, "Companion gefunden", v, md->offer_addr);
    }
}

/* POST /api/password/show: the web login for whoever stands at the deck. */
static void password_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_WEB, "Benutzer", "admin", "");
    card(put(p, at(p, 0, 1), K_NONE), ICON_PASSWORD, "Passwort", md->web_pass, "");
    if (ip_of(md)) {
        card(put(p, at(p, 0, 2), K_NONE), ICON_WEB, "Adresse", ip_of(md), fit(p, "im Browser", ""));
    }
    close_key(put(p, at(p, 0, -1), K_NONE), ICON_CLOSE, "Schließen", ""); /* any key closes the page */
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_PASSWORD, C_WARN, "Web-Passwort", md->web_pass,
                "Wird nach 30 s ausgeblendet");
    }
}

/* POST /api/password/reset: two taps on the deck itself reset every setting. */
static void reset_page(page_t *p)
{
    int key = at(p, 0, 1);
    bool asking = confirming(p->m, key, p->md->now_us);
    card(put(p, at(p, 0, 0), K_NONE), ICON_ALERT, "Passwort", fit(p, "vergessen?", "vergessen"),
         fit(p, "alles zurücksetzen", ""));
    ui_tile_t *reset = put(p, key, K_RESET);
    button(reset, ICON_RESET, asking ? fit(p, "Nochmal tippen", "Nochmal") : fit(p, "Zurücksetzen", "Reset"),
           fit(p, "Werkszustand", ""));
    reset->active = true;
    reset->color = C_DANGER;
    close_key(put(p, at(p, 0, -1), K_KEEP), ICON_CLOSE, "Abbrechen", "");
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_RESET, C_DANGER, "Passwort vergessen?",
                "Setzt alle Einstellungen zurück",
                "Zweimal „Zurücksetzen“ tippen. Danach startet das Setup.");
    }
}

/* POST /api/login/request: a browser asks to get in without the password. */
static void login_page(page_t *p)
{
    const ui_model_t *md = p->md;
    card(put(p, at(p, 0, 0), K_NONE), ICON_WEB, fit(p, "Login-Anfrage", "Login"), md->login_ip, "");
    ui_tile_t *yes = put(p, at(p, 0, 1), K_ACCEPT);
    button(yes, ICON_SAVE, "Annehmen", fit(p, "darf rein", ""));
    yes->active = true;
    yes->color = C_ACTIVE;
    ui_tile_t *no = put(p, at(p, 0, 2), K_DENY);
    button(no, ICON_CLOSE, "Ablehnen", fit(p, "bleibt draußen", ""));
    no->color = C_DANGER;
    int col = 0;
    if (md->login_who[0]) {
        card(put(p, at(p, 1, col++), K_NONE), ICON_WEB, "Gerät", md->login_who, "");
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
        snprintf(sub, sizeof(sub), "%s%s%snur annehmen, wenn du das bist", md->login_mac[0] ? "MAC " : "",
                 md->login_mac, md->login_mac[0] ? " · " : "");
        segment(&p->sc->seg[0], 4, ICON_WEB, C_WARN, "Login-Anfrage", md->login_who[0] ? v : md->login_ip,
                sub);
    }
}

/* Every host that can draw, as many as fit beside the back key: 7 on the Neo. */
static void sources_page(page_t *p)
{
    const ui_model_t *md = p->md;
    int back = at(p, 0, -1);
    close_key(put(p, back, K_BACK), ICON_CLOSE, "Zurück", fit(p, "ohne Wechsel", ""));
    int key = 0;
    for (int i = 0; i < md->source_count && key < p->l->cols * p->l->rows; i++) {
        key += key == back;
        source_key(p, key++, i, false);
    }
    if (p->l->strip) {
        segment(&p->sc->seg[0], 4, ICON_OPEN, C_EDIT, "Wer zeichnet?", "Welcher Rechner?",
                "Antippen, dann zeichnet dieser Rechner.");
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
    case PAGE_RESET:
        reset_page(&p);
        break;
    case PAGE_LOGIN_REQ:
        login_page(&p);
        break;
    default:
        home(&p);
        break;
    }
}

/* The page that shows: a connection request comes first, then a reset question, a login request, the
 * password, the running setup WLAN, a found Companion, then the welcome steps until setup is done. */
static ui_menu_t shown_menu(const ui_menu_t *m, const ui_model_t *md)
{
    ui_menu_t shown = *m;
    if (md->request[0]) {
        shown.page = PAGE_REQUEST;
    } else if (md->reset_ask) {
        shown.page = PAGE_RESET;
    } else if (md->login_ip[0]) {
        shown.page = PAGE_LOGIN_REQ;
    } else if (md->show_pass) {
        shown.page = PAGE_PASSWORD;
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
    char v[48];
    snprintf(v, sizeof(v), "Update %s: %d %%", md->update_what, md->update_pct);
    if (l->strip) {
        segment(&sc->seg[0], 4, ICON_UPDATE, C_EDIT, "Update", v,
                "Nicht vom Strom trennen, der DeckConnector startet danach neu");
    }
    char pct[12];
    snprintf(pct, sizeof(pct), "%d %%", md->update_pct);
    card(&sc->tiles[0], ICON_UPDATE, "Update", pct, l->key_px >= 110 ? md->update_what : "");
    if (!l->strip) {
        card(&sc->tiles[1], ICON_ALERT, "Strom", "nicht trennen", "");
    }
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
        snprintf(m->note, sizeof(m->note), "Werkszustand, starte neu");
    } else if (held < HOLD_SHOWN_US && confirmed(m, ev->index, md->now_us)) {
        act->what = UI_DO_RESTART;
        snprintf(m->note, sizeof(m->note), "Starte neu");
    }
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
    if (shown.page == PAGE_PASSWORD &&
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
    if (shown.page == PAGE_PASSWORD || (deck_only && !key)) {
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
                           what == K_ACCEPT || what == K_DENY)) {
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
        go(m, PAGE_LAN, md->now_us);
        return true;
    case K_AP:
        act->what = UI_DO_AP_START;
        go(m, PAGE_SETUP_AP, md->now_us);
        return true;
    case K_AP_CLOSE:
        act->what = UI_DO_AP_STOP;
        go(m, m->page != PAGE_SETUP_AP ? m->page : (md->setup_done ? PAGE_MAIN : PAGE_WELCOME_NET),
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
        m->field = what - K_FIELD0;
        return true;
    case K_DHCP:
        m->lan_dhcp = !m->lan_dhcp;
        return true;
    case K_LAN_CLOSE:
        go(m, PAGE_MAIN, md->now_us);
        return true;
    case K_SAVE_LAN:
        if (m->lan_dhcp == md->lan_dhcp &&
            (m->lan_dhcp || memcmp(m->octets, md->lan_cfg, sizeof(m->octets)) == 0)) {
            go(m, PAGE_MAIN, md->now_us);
            snprintf(m->note, sizeof(m->note), "LAN unverändert");
            return true;
        }
        *act = (ui_action_t){.what = UI_DO_SAVE_LAN, .dhcp = m->lan_dhcp};
        memcpy(act->octets, m->octets, sizeof(act->octets));
        snprintf(m->note, sizeof(m->note), "Gespeichert, starte neu");
        return true;
    case K_SKIP:
    case K_DONE:
        act->what = UI_DO_SETUP_DONE;
        go(m, PAGE_HOME, md->now_us);
        return true;
    default:
        if (what >= K_SRC0 && (int)what < K_SRC0 + md->source_count) {
            int i = what - K_SRC0;
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

bool ui_menu_chord(const char *chord, int encoders, int keys, bool *on_encoders, int *last)
{
    if (strcmp(chord, "off") == 0) {
        return false;
    }
    *on_encoders =
        encoders >= 2 && strcmp(chord, "keys") != 0; /* encoders: auto's choice when there are some */
    *last = (*on_encoders ? encoders : keys) - 1;
    return *last >= 1;
}
