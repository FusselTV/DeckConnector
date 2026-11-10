/* The deck pictures in the docs, drawn by the firmware's own db_ui code. Built and run by deck-shots.mjs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_driver.h"
#include "ui.h"

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

static void save(const db_device_info_t *dev, const ui_screen_t *sc, const char *dir, const char *name)
{
    ui_canvas_t c = {dev->displays[0].width, dev->displays[0].height, NULL};
    c.px = calloc((size_t)c.w * c.h, 3);
    db_rect_t dirty[DB_MAX_TARGETS + UI_SEGMENTS];
    ui_render(&c, dev, sc, NULL, dirty, DB_MAX_TARGETS + UI_SEGMENTS);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%u %u\n255\n", c.w, c.h);
    for (size_t i = 0; i < (size_t)c.w * c.h; i++) {
        const uint8_t *p = c.px + i * 3; /* B, G, R */
        fputc(p[2], f);
        fputc(p[1], f);
        fputc(p[0], f);
    }
    fclose(f);
    free(c.px);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    db_dev_t d = {.io = &io, .driver = db_driver_find(0x0fd9, 0x0084)};
    if (!d.driver || d.driver->open(&d, 0x0fd9, 0x0084) != DB_OK) {
        return 1;
    }
    ui_layout_t l = ui_layout_of(&d.info);
    ui_model_t md = {
        .now_us = 100000000,
        .controller = UI_CTRL_COMPANION,
        .hosts = {1, 1, 1},
        .sources = {{.kind = UI_CTRL_COMPANION, .via = UI_VIA_SATELLITE, .chosen = true, .label = "studio"},
                    {.kind = UI_CTRL_APP, .via = UI_VIA_DOCK, .label = "STUDIO-PC"},
                    {.kind = UI_CTRL_SERVER, .via = UI_VIA_SERVER, .label = "regie"}},
        .source_count = 3,
        .lan_link = true,
        .lan_up = true,
        .lan_dhcp = true,
        .wlan_chip = true,
        .wlan_up = true,
        .wlan_rssi = -58,
        .brightness = 80,
        .update_pct = -1,
        .menu_hold_ms = 2000,
        .setup_done = true,
    };
    strcpy(md.menu_chord, "auto");
    strcpy(md.lan_ip, "192.168.1.50");
    strcpy(md.wlan_ip, "192.168.1.51");
    strcpy(md.wifi_ssid, "studio");
    strcpy(md.web_pass, "k3t9m2xq");
    strcpy(md.fw, "v1.0.0");
    strcpy(md.wlan_fw, "2.0.4");
    ui_screen_t sc;

    ui_menu_t m = {.page = PAGE_MAIN, .confirm_key = -1};
    ui_menu_screen(&m, &md, &l, &sc);
    save(&d.info, &sc, dir, "menu");

    /* Companion is chosen but gone; the Elgato App and a server are connected. */
    md.hosts[UI_CTRL_COMPANION] = 0;
    md.sources[0] = md.sources[1];
    md.sources[1] = md.sources[2];
    md.source_count = 2;
    m = (ui_menu_t){.page = PAGE_HOME, .confirm_key = -1};
    ui_menu_screen(&m, &md, &l, &sc);
    save(&d.info, &sc, dir, "away");

    m.confirm_key = 1;
    m.confirm_until = md.now_us + 1;
    ui_menu_screen(&m, &md, &l, &sc);
    save(&d.info, &sc, dir, "asking");
    return 0;
}
