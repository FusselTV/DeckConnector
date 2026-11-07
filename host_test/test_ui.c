#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unity.h>

#include "db_driver.h"
#include "ui.h"

/* Every page in every state on every deck: nothing may be cut, and the pictures land in build/ui for review.
 */

static db_err_t no_write(const uint8_t *r, size_t len)
{
    (void)r;
    (void)len;
    return DB_OK;
}

static db_err_t no_flush(void)
{
    return DB_OK;
}

static db_err_t feature(uint8_t *buf, size_t len, size_t *out_len)
{
    memset(buf + 1, 0, len - 1);
    *out_len = len;
    return DB_OK;
}

static db_err_t set_feature(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return DB_OK;
}

static const db_hid_io_t io = {no_write, no_flush, feature, set_feature, 1024};

static const struct {
    uint16_t pid;
    const char *name;
} decks[] = {{0x0084, "plus"}, {0x0080, "mk2"}, {0x008f, "xl"}, {0x009a, "neo"}};

static db_device_info_t open_deck(uint16_t pid)
{
    db_dev_t dev = {.io = &io, .driver = db_driver_find(0x0fd9, pid)};
    TEST_ASSERT_NOT_NULL(dev.driver);
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, 0x0fd9, pid));
    return dev.info;
}

static ui_model_t base_model(void)
{
    ui_model_t md = {
        .now_us = 100000000,
        .controller = 1,
        .hosts = {2, 2, 1},
        .sources = {{.kind = 1, .via = UI_VIA_SATELLITE, .chosen = true, .label = "studio", .id = "satellite"},
                    {.kind = 1, .via = UI_VIA_DOCK, .label = "192.168.1.16", .id = "192.168.1.16"},
                    {.kind = 2, .via = UI_VIA_DOCK, .label = "DESKTOP-EXAMPLE", .id = "192.168.1.180"},
                    {.kind = 0,
                     .via = UI_VIA_SERVER,
                     .label = "192.168.1.128:8765",
                     .id = "ws://192.168.1.128:8765/x"},
                    {.kind = 0,
                     .via = UI_VIA_SERVER,
                     .label = "192.168.1.128:8766",
                     .id = "ws://192.168.1.128:8766/x"}},
        .source_count = 5,
        .lan_link = true,
        .lan_up = true,
        .lan_dhcp = true,
        .lan_cfg = {{192, 168, 1, 214}, {255, 255, 255, 0}, {192, 168, 1, 1}, {192, 168, 1, 1}},
        .wlan_chip = true,
        .wlan_up = true,
        .wlan_rssi = -67,
        .brightness = 100,
        .update_pct = -1,
        .menu_hold_ms = 2000,
        .setup_done = true};
    strcpy(md.menu_chord, "auto");
    strcpy(md.lan_ip, "192.168.1.214");
    strcpy(md.wlan_ip, "192.168.1.237");
    strcpy(md.wifi_ssid, "deckbridge");
    strcpy(md.ap_ssid, "deckbridge-a1b2c3");
    strcpy(md.ap_pass, "k7m2p9xq");
    strcpy(md.ap_ip, "192.168.4.1");
    strcpy(md.server_url, "ws://192.168.1.128:8765/deckbridge");
    strcpy(md.web_pass, "bench-pass-12");
    strcpy(md.fw, "v0.1.0-20-ge4fdacd-dirty");
    strcpy(md.wlan_fw, "3.0.10");
    md.ap_qr.size =
        29; /* a stand-in pattern: the host build has no QR encoder, the bench reads the real one */
    for (int i = 0; i < 29 * 29; i++) {
        md.ap_qr.bits[i / 8] |= (uint8_t)(((i * 7) % 3 == 0) << (i % 8));
    }
    return md;
}

/* The longest values the bridge allows, and the states that change texts. */
static ui_model_t variant(int v)
{
    ui_model_t md = base_model();
    switch (v) {
    case 1: /* long everything */
        strcpy(md.wifi_ssid, "FRITZ!Box 7590 XY Gast");
        strcpy(md.server_url, "ws://a-very-long-server-name.int.example.com:65535/deckbridge/path/x");
        strcpy(md.web_pass, "abcdefghijklmnopqrstuvw");
        strcpy(md.lan_ip, "255.255.255.255");
        strcpy(md.wlan_ip, "255.255.255.255");
        strcpy(md.ap_ssid, "deckbridge-ffffff");
        strcpy(md.ap_pass, "abcdefghijklmno");
        strcpy(md.fw, "v10.20.300-4000-gabcdef0-dirty");
        strcpy(md.wlan_fw, "10.20.300");
        md.brightness = 100;
        md.controller = 2;
        md.sources[0].chosen = false;
        md.sources[2].chosen = true;
        strcpy(md.sources[2].label, "studio-pc-regie-links-2");
        strcpy(md.sources[1].label, "255.255.255.255");
        break;
    case 2: /* no network at all */
        md.lan_link = md.lan_up = md.wlan_up = false;
        md.wifi_ssid[0] = '\0';
        md.controller = 0;
        md.hosts[1] = md.hosts[2] = 0;
        md.source_count = 0;
        break;
    case 3: /* server chosen and away, nothing configured, no WLAN chip */
        md.controller = 0;
        md.sources[0].chosen = false;
        md.server_url[0] = '\0';
        md.wlan_chip = false;
        md.wlan_up = false;
        md.lan_dhcp = false;
        break;
    default:
        break;
    }
    return md;
}

static void save_ppm(const ui_canvas_t *c, const char *name)
{
    mkdir(UI_OUT, 0755);
    char path[256];
    snprintf(path, sizeof(path), UI_OUT "/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "P6\n%u %u\n255\n", c->w, c->h);
    for (size_t i = 0; i < (size_t)c->w * c->h; i++) {
        const uint8_t *p = c->px + i * 3; /* the canvas holds B, G, R */
        fputc(p[2], f);
        fputc(p[1], f);
        fputc(p[0], f);
    }
    fclose(f);
}

static int cut_pages;

static void print_cut(const char *text, int room)
{
    printf("  \"%s\" in %d px\n", text, room);
}

static void check_page(const db_device_info_t *dev, const ui_screen_t *sc, const char *name)
{
    ui_canvas_t c = {dev->displays[0].width, dev->displays[0].height, NULL};
    c.px = calloc((size_t)c.w * c.h, 3);
    db_rect_t dirty[DB_MAX_TARGETS + UI_SEGMENTS];
    int before = ui_text_cuts;
    int n = ui_render(&c, dev, sc, NULL, dirty, DB_MAX_TARGETS + UI_SEGMENTS);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    save_ppm(&c, name);
    free(c.px);
    if (ui_text_cuts != before) { /* a text did not fit: the picture is in build/ui */
        printf("cut: %s\n", name);
        cut_pages++;
    }
}

static void test_no_page_cuts_a_text(void)
{
    static const ui_page_t pages[] = {PAGE_HOME,     PAGE_MAIN,        PAGE_LAN,
                                      PAGE_SETUP_AP, PAGE_WELCOME_NET, PAGE_SOURCES};
    static const char *const page_names[] = {"home", "menu", "lan", "setup_ap", "welcome_net", "sources"};
    for (size_t d = 0; d < sizeof(decks) / sizeof(decks[0]); d++) {
        db_device_info_t dev = open_deck(decks[d].pid);
        ui_layout_t l = ui_layout_of(&dev);
        for (int v = 0; v < 4; v++) {
            ui_model_t md = variant(v);
            for (size_t p = 0; p < sizeof(pages) / sizeof(pages[0]); p++) {
                for (int fixed = 0; fixed < (pages[p] == PAGE_LAN ? 2 : 1); fixed++) {
                    ui_menu_t m = {.page = pages[p], .confirm_key = -1, .lan_dhcp = !fixed};
                    memcpy(m.octets, md.lan_cfg, sizeof(m.octets));
                    ui_screen_t sc;
                    ui_menu_screen(&m, &md, &l, &sc);
                    char name[64];
                    snprintf(name, sizeof(name), "%s_%s%s_%d", decks[d].name, page_names[p],
                             fixed ? "_fixed" : "", v);
                    check_page(&dev, &sc, name);
                }
            }
            ui_menu_t m = {.page = PAGE_HOME, .confirm_key = 1, .confirm_until = md.now_us + 1};
            ui_screen_t asking; /* the second host waits for its second tap */
            ui_menu_screen(&m, &md, &l, &asking);
            char asked[64];
            snprintf(asked, sizeof(asked), "%s_asking_%d", decks[d].name, v);
            check_page(&dev, &asking, asked);
            m.confirm_key = -1;
            md.setup_done = false;
            ui_screen_t sc;
            ui_menu_screen(&m, &md, &l, &sc);
            char name[64];
            snprintf(name, sizeof(name), "%s_welcome_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
            strcpy(md.request, "192.168.1.255");
            strcpy(md.request_name, v == 1 ? "studio-pc-regie-links-2" : "DESKTOP-EXAMPLE");
            ui_menu_screen(&m, &md, &l, &sc);
            snprintf(name, sizeof(name), "%s_request_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
            md.request[0] = '\0';
            strcpy(md.offer_name, v == 1 ? "studio-pc-regie-links-2" : "studio");
            strcpy(md.offer_addr, v == 1 ? "255.255.255.255:65535" : "192.168.1.15:16622");
            ui_menu_screen(&m, &md, &l, &sc);
            snprintf(name, sizeof(name), "%s_offer_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
            md.offer_addr[0] = '\0';
            md.show_pass = true;
            ui_menu_screen(&m, &md, &l, &sc);
            snprintf(name, sizeof(name), "%s_password_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
            md.show_pass = false;
            md.reset_ask = true;
            for (int again = 0; again < 2; again++) {
                m.confirm_key = again ? 1 : -1;
                ui_menu_screen(&m, &md, &l, &sc);
                snprintf(name, sizeof(name), "%s_reset%s_%d", decks[d].name, again ? "_again" : "", v);
                check_page(&dev, &sc, name);
            }
            m.confirm_key = -1;
            md.reset_ask = false;
            md.source_count = 0; /* a new deck: nobody connected yet */
            md.menu_hold_ms = v == 1 ? 2500 : 2000;
            strcpy(md.menu_chord, v == 2 ? "keys" : (v == 3 ? "off" : "auto"));
            ui_menu_screen(&m, &md, &l, &sc);
            snprintf(name, sizeof(name), "%s_new_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
            md.update_pct = 100;
            strcpy(md.update_what, "WLAN-Chip");
            ui_update_screen(&md, &l, &sc);
            snprintf(name, sizeof(name), "%s_update_%d", decks[d].name, v);
            check_page(&dev, &sc, name);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cut_pages, "pages with a cut text, see above");
}

static bool press(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, uint8_t key, ui_action_t *act)
{
    db_input_t down = {.kind = DB_INPUT_KEY_DOWN, .index = key};
    db_input_t up = {.kind = DB_INPUT_KEY_UP, .index = key};
    ui_menu_input(m, md, l, &down, act);
    return ui_menu_input(m, md, l, &up, act);
}

static void test_neo_keeps_brightness_and_password(void)
{
    db_device_info_t dev = open_deck(0x009a);
    ui_layout_t l = ui_layout_of(&dev);
    TEST_ASSERT_EQUAL_UINT8(2, l.rows);
    ui_model_t md = base_model();
    md.brightness = 90;
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;
    press(&m, &md, &l, 6, &act); /* home, bottom row, third key: one step up, past 100 back to 20 */
    TEST_ASSERT_EQUAL_INT(UI_DO_BRIGHTNESS, act.what);
    TEST_ASSERT_EQUAL_INT(10, act.value);
    md.brightness = 100;
    press(&m, &md, &l, 6, &act);
    TEST_ASSERT_EQUAL_INT(-80, act.value);
    ui_screen_t sc;
    m.page = PAGE_MAIN;
    ui_menu_screen(&m, &md, &l, &sc);
    TEST_ASSERT_EQUAL_STRING(md.web_pass, sc.tiles[6].value);
}

static void test_flows_on_the_plus(void)
{
    db_device_info_t dev = open_deck(0x0084);
    ui_layout_t l = ui_layout_of(&dev);
    TEST_ASSERT_EQUAL_UINT8(4, l.cols);
    TEST_ASSERT_EQUAL_UINT8(2, l.rows);
    TEST_ASSERT_TRUE(l.strip);
    ui_model_t md = base_model();
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;
    ui_screen_t sc;
    ui_menu_screen(&m, &md, &l, &sc); /* the chosen host first, then the next, then "Alle" for five */
    TEST_ASSERT_EQUAL_STRING("studio", sc.tiles[0].value);
    TEST_ASSERT_EQUAL_STRING("Satellite", sc.tiles[0].sub);
    TEST_ASSERT_TRUE(sc.tiles[0].active);
    TEST_ASSERT_EQUAL_STRING("Alle", sc.tiles[2].label);

    press(&m, &md, &l, 1, &act); /* home: the first tap only asks */
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    press(&m, &md, &l, 1, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
    TEST_ASSERT_EQUAL_INT(1, act.value);

    press(&m, &md, &l, 2, &act); /* "Alle": a page of its own, then one tap picks */
    TEST_ASSERT_EQUAL_INT(PAGE_SOURCES, m.page);
    press(&m, &md, &l, 4, &act); /* key 3 is "Zurück", so the fourth host sits on key 4 */
    TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
    TEST_ASSERT_EQUAL_INT(3, act.value);
    TEST_ASSERT_EQUAL_INT(PAGE_HOME, m.page);

    md.sources[0].chosen = false; /* the chosen program is away: an amber key says so, and does nothing */
    ui_menu_screen(&m, &md, &l, &sc);
    TEST_ASSERT_EQUAL_STRING("getrennt", sc.tiles[0].value);
    press(&m, &md, &l, 0, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    md.sources[0].chosen = true;

    press(&m, &md, &l, 3, &act); /* menu */
    TEST_ASSERT_TRUE(ui_menu_open(&m));
    db_input_t turn = {.kind = DB_INPUT_ENCODER_TURN, .index = 0, .value = -2};
    ui_menu_input(&m, &md, &l, &turn, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_BRIGHTNESS, act.what);
    TEST_ASSERT_EQUAL_INT(-10, act.value);
    press(&m, &md, &l, 1, &act); /* in the menu one tap picks and closes */
    TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
    TEST_ASSERT_FALSE(ui_menu_open(&m));

    ui_menu_open_main(&m, md.now_us); /* the chord's release must not close it again */
    db_input_t release = {.kind = DB_INPUT_ENCODER_UP, .index = 3};
    ui_menu_input(&m, &md, &l, &release, &act);
    TEST_ASSERT_TRUE(ui_menu_open(&m));

    press(&m, &md, &l, 7, &act); /* restart asks first */
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    press(&m, &md, &l, 7, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_RESTART, act.what);

    ui_menu_open_main(&m, md.now_us);
    press(&m, &md, &l, 4, &act); /* LAN editor: switch to fixed, change a number, save */
    TEST_ASSERT_EQUAL_INT(PAGE_LAN, m.page);
    press(&m, &md, &l, 5, &act);
    db_input_t up = {.kind = DB_INPUT_ENCODER_TURN, .index = 3, .value = 1};
    ui_menu_input(&m, &md, &l, &up, &act);
    press(&m, &md, &l, 7, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_SAVE_LAN, act.what);
    TEST_ASSERT_FALSE(act.dhcp);
    TEST_ASSERT_EQUAL_UINT8(215, act.octets[0][3]);
}

static void test_flows_without_encoders(void)
{
    db_device_info_t dev = open_deck(0x0080);
    ui_layout_t l = ui_layout_of(&dev);
    TEST_ASSERT_EQUAL_UINT8(5, l.cols);
    TEST_ASSERT_EQUAL_UINT8(3, l.rows);
    TEST_ASSERT_FALSE(l.strip);
    ui_model_t md = base_model();
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;
    press(&m, &md, &l, 11, &act); /* brightness keys in the bottom row */
    TEST_ASSERT_EQUAL_INT(UI_DO_BRIGHTNESS, act.what);
    TEST_ASSERT_EQUAL_INT(10, act.value);
    press(&m, &md, &l, 4, &act); /* the menu sits top right on every deck */
    TEST_ASSERT_TRUE(ui_menu_open(&m));
    press(&m, &md, &l, 4, &act);
    TEST_ASSERT_FALSE(ui_menu_open(&m));
}

/* auto takes the outer encoders when there are some, else the outer keys; keys and off as named */
static void test_menu_chord_on_every_deck(void)
{
    for (size_t i = 0; i < sizeof(decks) / sizeof(decks[0]); i++) {
        db_device_info_t dev = open_deck(decks[i].pid);
        bool enc;
        int last;
        TEST_ASSERT_TRUE(ui_menu_chord("auto", dev.encoder_count, dev.key_count, &enc, &last));
        TEST_ASSERT_EQUAL(dev.encoder_count >= 2, enc);
        TEST_ASSERT_EQUAL((enc ? dev.encoder_count : dev.key_count) - 1, last);
        TEST_ASSERT_TRUE(ui_menu_chord("encoders", dev.encoder_count, dev.key_count, &enc, &last));
        TEST_ASSERT_EQUAL(dev.encoder_count >= 2, enc);
        TEST_ASSERT_TRUE(ui_menu_chord("keys", dev.encoder_count, dev.key_count, &enc, &last));
        TEST_ASSERT_FALSE(enc);
        TEST_ASSERT_EQUAL(dev.key_count - 1, last);
        TEST_ASSERT_FALSE(ui_menu_chord("off", dev.encoder_count, dev.key_count, &enc, &last));
    }
}

static void test_welcome(void)
{
    db_device_info_t dev = open_deck(0x0084);
    ui_layout_t l = ui_layout_of(&dev);
    ui_model_t md = base_model();
    md.setup_done = false;
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;
    press(&m, &md, &l, 1, &act); /* first start: one tap picks, no confirm */
    TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
    TEST_ASSERT_EQUAL_INT(PAGE_WELCOME_NET, m.page);
    press(&m, &md, &l, 3, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_SETUP_DONE, act.what);
    TEST_ASSERT_EQUAL_INT(PAGE_HOME, m.page);
}

static void test_setup_shows_the_login_code_and_the_wizard_step(void)
{
    db_device_info_t dev = open_deck(0x0084);
    ui_layout_t l = ui_layout_of(&dev);
    ui_model_t md = base_model();
    md.setup_done = false;
    md.login_qr = md.ap_qr;
    strcpy(md.login_token, "0123456789abcdef");
    md.setup_step = 3;
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    static ui_screen_t sc;
    ui_menu_screen(&m, &md, &l, &sc);
    TEST_ASSERT_EQUAL_INT(TILE_QR, sc.tiles[l.cols + 2].kind);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", sc.tiles[l.cols + 2].value);
    TEST_ASSERT_EQUAL_STRING("3/6", sc.tiles[2 * l.cols - 1].value);
    md.login_qr.size = 0;
    ui_menu_screen(&m, &md, &l, &sc);
    TEST_ASSERT_NOT_EQUAL_INT(TILE_QR, sc.tiles[l.cols + 2].kind);
}

/* No web UI: a new deck shows its address and the menu chord, the first host draws by itself, and a Companion
 * found over mDNS is taken with one key. */
static void test_without_the_web_ui(void)
{
    for (size_t d = 0; d < sizeof(decks) / sizeof(decks[0]); d++) {
        db_device_info_t dev = open_deck(decks[d].pid);
        ui_layout_t l = ui_layout_of(&dev);
        ui_model_t md = base_model();
        md.setup_done = false;
        md.source_count = 0;
        ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
        ui_screen_t sc;
        ui_menu_screen(&m, &md, &l, &sc);
        bool address = false;
        bool chord = false;
        for (int i = 0; i < UI_SEGMENTS; i++) {
            address |= strcmp(sc.seg[i].value, "192.168.1.214") == 0;
            chord |= strstr(sc.seg[i].value, "1 + ") != NULL;
        }
        for (int i = 0; i < UI_MAX_KEYS; i++) {
            address |= strcmp(sc.tiles[i].value, "192.168.1.214") == 0;
            chord |= strstr(sc.tiles[i].value, "1 + ") != NULL;
        }
        TEST_ASSERT_TRUE_MESSAGE(address, decks[d].name);
        TEST_ASSERT_TRUE_MESSAGE(chord, decks[d].name);
        ui_action_t act;
        TEST_ASSERT_FALSE(ui_first_source(&md, &act)); /* nobody there yet */
        md.source_count = 1; /* the Stream Deck app or Companion connects */
        md.picked = true;
        TEST_ASSERT_FALSE(ui_first_source(&md, &act)); /* a choice made before stays, e.g. a re-run setup */
        md.picked = false;
        TEST_ASSERT_TRUE(ui_first_source(&md, &act));
        TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
        TEST_ASSERT_EQUAL_INT(0, act.value);
        md.setup_done = true; /* chosen once: later hosts are picked on the deck */
        TEST_ASSERT_FALSE(ui_first_source(&md, &act));

        md.setup_done = false;
        strcpy(md.offer_name, "studio");
        strcpy(md.offer_addr, "192.168.1.15:16622");
        TEST_ASSERT_FALSE(ui_first_source(&md, &act)); /* the offer comes first */
        ui_menu_screen(&m, &md, &l, &sc);
        TEST_ASSERT_EQUAL_STRING("studio", sc.tiles[0].value);
        press(&m, &md, &l, 1, &act);
        TEST_ASSERT_EQUAL_INT(UI_DO_OFFER, act.what);
        TEST_ASSERT_EQUAL_INT(1, act.value);
        press(&m, &md, &l, 2, &act);
        TEST_ASSERT_EQUAL_INT(UI_DO_OFFER, act.what);
        TEST_ASSERT_EQUAL_INT(0, act.value);
        md.offer_addr[0] = '\0';
        md.request[0] = '\0';
        strcpy(md.request, "192.168.1.77"); /* a pairing request still comes before an offer */
        strcpy(md.offer_addr, "192.168.1.15:16622");
        press(&m, &md, &l, 1, &act);
        TEST_ASSERT_EQUAL_INT(UI_DO_ANSWER, act.what);
    }
}

static void test_password_and_reset_pages(void)
{
    db_device_info_t dev = open_deck(0x0084);
    ui_layout_t l = ui_layout_of(&dev);
    ui_model_t md = base_model();
    ui_menu_t m = {.page = PAGE_MAIN, .confirm_key = -1};
    ui_action_t act;
    md.show_pass = true;
    press(&m, &md, &l, 5, &act); /* any key hides the password */
    TEST_ASSERT_EQUAL_INT(UI_DO_HIDE_PASSWORD, act.what);
    TEST_ASSERT_EQUAL_INT(PAGE_MAIN, m.page);

    md.reset_ask = true;         /* comes first */
    press(&m, &md, &l, 1, &act); /* sent over the API: nothing */
    press(&m, &md, &l, 1, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    md.from_deck = true;
    press(&m, &md, &l, 1, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    ui_screen_t sc;
    ui_menu_screen(&m, &md, &l, &sc);
    TEST_ASSERT_EQUAL_STRING("Nochmal tippen", sc.tiles[1].label);
    press(&m, &md, &l, 1, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_RESET, act.what);
    press(&m, &md, &l, 3, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_CANCEL_RESET, act.what);
}

void setUp(void)
{
    ui_on_cut = print_cut;
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_page_cuts_a_text);
    RUN_TEST(test_flows_on_the_plus);
    RUN_TEST(test_neo_keeps_brightness_and_password);
    RUN_TEST(test_flows_without_encoders);
    RUN_TEST(test_welcome);
    RUN_TEST(test_setup_shows_the_login_code_and_the_wizard_step);
    RUN_TEST(test_menu_chord_on_every_deck);
    RUN_TEST(test_without_the_web_ui);
    RUN_TEST(test_password_and_reset_pages);
    return UNITY_END();
}
