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
        .sources = {{.kind = 1, .chosen = true, .label = "192.168.1.15", .id = "192.168.1.15"},
                    {.kind = 1, .label = "192.168.1.16", .id = "192.168.1.16"},
                    {.kind = 2, .chosen = true, .label = "192.168.1.180", .id = "192.168.1.180"},
                    {.kind = 0,
                     .chosen = true,
                     .label = "192.168.1.128:8765",
                     .id = "ws://192.168.1.128:8765/x"},
                    {.kind = 0, .label = "192.168.1.128:8766", .id = "ws://192.168.1.128:8766/x"}},
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
        .setup_done = true};
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
        break;
    case 2: /* no network at all */
        md.lan_link = md.lan_up = md.wlan_up = false;
        md.wifi_ssid[0] = '\0';
        md.controller = 0;
        md.hosts[1] = md.hosts[2] = 0;
        md.source_count = 0;
        break;
    case 3: /* server chosen, nothing configured, no WLAN chip */
        md.controller = 0;
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
    mkdir("ui", 0755);
    char path[96];
    snprintf(path, sizeof(path), "ui/%s.ppm", name);
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
                    ui_menu_t m = {
                        .page = pages[p], .confirm_key = -1, .lan_dhcp = !fixed, .pick_kind = v % 2 ? 0 : 1};
                    memcpy(m.octets, md.lan_cfg, sizeof(m.octets));
                    ui_screen_t sc;
                    ui_menu_screen(&m, &md, &l, &sc);
                    char name[64];
                    snprintf(name, sizeof(name), "%s_%s%s_%d", decks[d].name, page_names[p],
                             fixed ? "_fixed" : "", v);
                    check_page(&dev, &sc, name);
                }
            }
            ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
            md.setup_done = false;
            ui_screen_t sc;
            ui_menu_screen(&m, &md, &l, &sc);
            char name[64];
            snprintf(name, sizeof(name), "%s_welcome_%d", decks[d].name, v);
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
    md.hosts[UI_CTRL_SERVER] = 1; /* one server: its key picks it, no list */
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;

    press(&m, &md, &l, 2, &act); /* home: the first tap only asks; key 2 is the server */
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    press(&m, &md, &l, 2, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_CONTROLLER, act.what);
    TEST_ASSERT_EQUAL_INT(UI_CTRL_SERVER, act.value);

    press(&m, &md, &l, 0, &act); /* two Companions: their own page, then one tap picks one */
    TEST_ASSERT_EQUAL_INT(PAGE_SOURCES, m.page);
    TEST_ASSERT_EQUAL_INT(UI_DO_NOTHING, act.what);
    press(&m, &md, &l, 1, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_SOURCE, act.what);
    TEST_ASSERT_EQUAL_INT(1, act.value);
    TEST_ASSERT_EQUAL_INT(PAGE_HOME, m.page);

    press(&m, &md, &l, 3, &act); /* menu */
    TEST_ASSERT_TRUE(ui_menu_open(&m));
    db_input_t turn = {.kind = DB_INPUT_ENCODER_TURN, .index = 0, .value = -2};
    ui_menu_input(&m, &md, &l, &turn, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_BRIGHTNESS, act.what);
    TEST_ASSERT_EQUAL_INT(-10, act.value);
    press(&m, &md, &l, 2, &act); /* in the menu one tap picks and closes */
    TEST_ASSERT_EQUAL_INT(UI_DO_CONTROLLER, act.what);
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

static void test_welcome(void)
{
    db_device_info_t dev = open_deck(0x0084);
    ui_layout_t l = ui_layout_of(&dev);
    ui_model_t md = base_model();
    md.setup_done = false;
    ui_menu_t m = {.page = PAGE_HOME, .confirm_key = -1};
    ui_action_t act;
    press(&m, &md, &l, 1, &act); /* first start: one tap picks, no confirm */
    TEST_ASSERT_EQUAL_INT(UI_DO_CONTROLLER, act.what);
    TEST_ASSERT_EQUAL_INT(PAGE_WELCOME_NET, m.page);
    press(&m, &md, &l, 3, &act);
    TEST_ASSERT_EQUAL_INT(UI_DO_SETUP_DONE, act.what);
    TEST_ASSERT_EQUAL_INT(PAGE_HOME, m.page);
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
    return UNITY_END();
}
