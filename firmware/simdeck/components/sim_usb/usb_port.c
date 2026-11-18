/*
 * The USB port with emulated decks: DB_SIM_DECK_MODELS="vid:pid,..." (hex, up to USB_PORT_DEVS) plugs them in
 * at start, Elgato ones as host_test/deck_emu.c, Mirabox ones as mirabox_emu.c. With DB_SIM_DECK_DIR set,
 * each image a deck takes lands in <dir>/<n>/ (n the plug position from 0) as key-<i>.jpg/.bmp, lcd.jpg,
 * window.jpg or window-<x>-<y>-<w>-<h>.jpg (Elgato) or id-<hex>.jpg (Mirabox), as sent: turned as the deck
 * wants it, and <dir>/<n>/brightness as the deck got it last (Mirabox: the LIG byte). <dir>/ctl is a FIFO
 * that takes one line per action, as a person at the deck: <n> key <i> <1|0>          Elgato key, sensors
 * after the image keys <n> enc <i> <1|0>          Elgato encoder press <n> turn <i> <ticks>       Elgato
 * encoder turn <n> touch <1|2|3> x y [x2 y2]   Elgato tap, long press, flick, window coordinates <n> rc <row>
 * <col> <1|0|-1|+1>  Mirabox press, release; -1 and +1 turn (or swipe) there <n> unplug | plug
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "deck_emu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mirabox_emu.h"
#include "usb_port.h"

typedef struct {
    db_hid_io_t io; /* first: the callbacks find their device by it */
    uint16_t vid, pid;
    bool elgato;
    bool plugged;
    bool open;
    int brightness;
} sim_dev_t;

static sim_dev_t devs[USB_PORT_DEVS];
static int dev_count;
static usb_port_cb_t cb;
static SemaphoreHandle_t lock; /* the emulators keep a current deck */
static const char *dir;

static const db_hid_io_t *emu_io(sim_dev_t *d)
{
    int n = (int)(d - devs);
    if (!d->elgato) {
        return mirabox_emu_io(n);
    }
    deck_emu_select(n);
    return deck_emu_io();
}

static void save(int deck, const char *name, const uint8_t *data, size_t len)
{
    char path[256];
    char tmp[264];
    snprintf(path, sizeof(path), "%s/%d", dir, deck);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%d/%s", dir, deck, name);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(data, 1, len, f) == len;
    ok = f && fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) { /* a reader never sees half an image */
        fprintf(stderr, "sim_usb: cannot save %s\n", path);
        unlink(tmp);
    }
}

static void elgato_image(int deck, const deck_image_t *im)
{
    char name[64];
    const char *ext = im->data[0] == 'B' ? "bmp" : "jpg";
    if (im->cmd == 0x07 || im->cmd == 0x01) {
        snprintf(name, sizeof(name), "key-%u.%s", im->key, ext);
    } else if (im->cmd == 0x08) {
        snprintf(name, sizeof(name), "lcd.jpg");
    } else if (im->cmd == 0x0b) {
        snprintf(name, sizeof(name), "window.jpg");
    } else {
        snprintf(name, sizeof(name), "window-%u-%u-%u-%u.jpg", im->win.x, im->win.y, im->win.w, im->win.h);
    }
    if (dir) {
        save(deck, name, im->data, im->len);
    }
}

static void mirabox_image(int deck, uint8_t id, const uint8_t *data, size_t len)
{
    char name[16];
    snprintf(name, sizeof(name), "id-%02x.jpg", id);
    if (dir) {
        save(deck, name, data, len);
    }
}

/* under lock */
static void note_brightness(sim_dev_t *d)
{
    int n = (int)(d - devs);
    int b = d->elgato ? deck_emu_brightness() : mirabox_emu_brightness(n);
    if (dir && b != d->brightness) {
        char text[8];
        d->brightness = b;
        save(n, "brightness", (const uint8_t *)text, (size_t)snprintf(text, sizeof(text), "%d", b));
    }
}

static db_err_t sim_write(const db_hid_io_t *io, const uint8_t *report, size_t len)
{
    sim_dev_t *d = (sim_dev_t *)io;
    xSemaphoreTake(lock, portMAX_DELAY);
    db_err_t err = d->plugged ? emu_io(d)->write(emu_io(d), report, len) : DB_ERR_IO;
    note_brightness(d);
    xSemaphoreGive(lock);
    return err;
}

static db_err_t sim_flush(const db_hid_io_t *io)
{
    return ((sim_dev_t *)io)->plugged ? DB_OK : DB_ERR_IO;
}

static db_err_t sim_get_feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    sim_dev_t *d = (sim_dev_t *)io;
    xSemaphoreTake(lock, portMAX_DELAY);
    const db_hid_io_t *e = emu_io(d);
    db_err_t err = d->plugged && e->get_feature ? e->get_feature(e, buf, len, out_len) : DB_ERR_IO;
    xSemaphoreGive(lock);
    return err;
}

static db_err_t sim_set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    sim_dev_t *d = (sim_dev_t *)io;
    xSemaphoreTake(lock, portMAX_DELAY);
    const db_hid_io_t *e = emu_io(d);
    db_err_t err = d->plugged && e->set_feature ? e->set_feature(e, buf, len) : DB_ERR_IO;
    note_brightness(d);
    xSemaphoreGive(lock);
    return err;
}

static void plug(int n, bool in)
{
    sim_dev_t *d = &devs[n];
    if (d->plugged == in) {
        return;
    }
    if (in) {
        xSemaphoreTake(lock, portMAX_DELAY);
        if (d->elgato) {
            deck_emu_select(n);
            deck_emu_start(d->pid); /* a deck that was unplugged forgot its picture */
        } else {
            mirabox_emu_start(n, d->vid, d->pid);
        }
        d->plugged = true;
        d->brightness = -2; /* written again on the first change */
        xSemaphoreGive(lock);
        cb.attached((uint8_t)(n + 1));
    } else {
        d->plugged = false;
        if (d->open) {
            cb.detached(n);
        }
    }
}

static void act(char *line)
{
    int n;
    char what[8];
    int a[5] = {0};
    int got = sscanf(line, "%d %7s %d %d %d %d %d", &n, what, &a[0], &a[1], &a[2], &a[3], &a[4]);
    static const struct {
        const char *verb;
        int args;
    } verbs[] = {{"unplug", 2}, {"plug", 2}, {"key", 4}, {"enc", 4}, {"turn", 4}, {"touch", 5}, {"rc", 5}};
    int need = 0;
    for (size_t i = 0; got >= 2 && i < sizeof(verbs) / sizeof(verbs[0]); i++) {
        need = strcmp(what, verbs[i].verb) ? need : verbs[i].args;
    }
    if (!need || got < need || n < 0 || n >= dev_count) {
        fprintf(stderr, "sim_usb: bad line: %s\n", line);
        return;
    }
    if (!strcmp(what, "unplug") || !strcmp(what, "plug")) {
        plug(n, what[0] == 'p');
        return;
    }
    uint8_t r[512];
    size_t len = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (devs[n].elgato) {
        deck_emu_select(n);
    }
    if (!devs[n].plugged || !devs[n].open || devs[n].elgato != (strcmp(what, "rc") != 0)) {
        len = 0;
    } else if (!strcmp(what, "key")) {
        len = deck_emu_key((uint8_t)a[0], a[1], r, sizeof(r));
    } else if (!strcmp(what, "enc")) {
        len = deck_emu_encoder_press((uint8_t)a[0], a[1], r, sizeof(r));
    } else if (!strcmp(what, "turn")) {
        len = deck_emu_encoder_turn((uint8_t)a[0], (int8_t)a[1], r, sizeof(r));
    } else if (!strcmp(what, "touch")) {
        len = deck_emu_touch((uint8_t)a[0], (uint16_t)a[1], (uint16_t)a[2], (uint16_t)a[3], (uint16_t)a[4], r,
                             sizeof(r));
    } else if (!strcmp(what, "rc")) {
        bool turn = strchr(line, '+') || a[2] < 0;
        len = mirabox_emu_input(n, (uint8_t)a[0], (uint8_t)a[1], turn, a[2], r, sizeof(r));
    }
    xSemaphoreGive(lock);
    if (len && len != MIRABOX_EMU_NONE) {
        cb.input(n, r, len, (uint64_t)esp_timer_get_time());
    } else if (!len) {
        fprintf(stderr, "sim_usb: no such input: %s\n", line);
    }
}

static void ctl_task(void *arg)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/ctl", dir);
    unlink(path);
    mkfifo(path, 0600);
    int fd = open(path, O_RDWR | O_NONBLOCK); /* RDWR: no EOF when a writer leaves */
    if (fd < 0) {
        perror("sim_usb: ctl");
        abort();
    }
    char buf[512];
    size_t have = 0;
    for (;;) {
        ssize_t n = read(fd, buf + have, sizeof(buf) - 1 - have);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        have += (size_t)n;
        buf[have] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {
            *nl = 0;
            act(buf);
            have -= (size_t)(nl + 1 - buf);
            memmove(buf, nl + 1, have + 1);
        }
        if (have == sizeof(buf) - 1) {
            fprintf(stderr, "sim_usb: line too long, dropped\n");
            have = 0;
        }
    }
}

esp_err_t usb_port_start(const usb_port_cb_t *callbacks, bool full_only)
{
    cb = *callbacks;
    lock = xSemaphoreCreateMutex();
    deck_emu_sink = elgato_image;
    mirabox_emu_sink = mirabox_image;
    dir = getenv("DB_SIM_DECK_DIR");
    const char *models = getenv("DB_SIM_DECK_MODELS");
    for (const char *p = models ? models : "0fd9:0084"; p && *p && dev_count < USB_PORT_DEVS;) {
        unsigned vid, pid;
        if (sscanf(p, "%x:%x", &vid, &pid) != 2) {
            fprintf(stderr, "sim_usb: DB_SIM_DECK_MODELS: no vid:pid at \"%s\"\n", p);
            abort();
        }
        if (vid == 0x0fd9 ? !deck_model((uint16_t)pid)
                          : !mirabox_emu_start(dev_count, (uint16_t)vid, (uint16_t)pid)) {
            fprintf(stderr, "sim_usb: DB_SIM_DECK_MODELS: no emulated deck %04x:%04x\n", vid, pid);
            abort();
        }
        devs[dev_count++] = (sim_dev_t){.io = {.write = sim_write,
                                               .flush = sim_flush,
                                               .get_feature = sim_get_feature,
                                               .set_feature = sim_set_feature},
                                        .vid = (uint16_t)vid,
                                        .pid = (uint16_t)pid,
                                        .elgato = vid == 0x0fd9};
        p = strchr(p, ',');
        p = p ? p + 1 : NULL;
    }
    if (dir) {
        xTaskCreate(ctl_task, "sim_ctl", 4096, NULL, 5, NULL);
    }
    for (int n = 0; n < dev_count; n++) {
        plug(n, true);
    }
    return ESP_OK;
}

esp_err_t usb_port_ids(uint8_t address, uint16_t *vid, uint16_t *pid)
{
    sim_dev_t *d = &devs[address - 1];
    if (address < 1 || address > dev_count || !d->plugged) {
        return ESP_ERR_NOT_FOUND;
    }
    *vid = d->vid;
    *pid = d->pid;
    return ESP_OK;
}

esp_err_t usb_port_open(uint8_t address, int *index, uint16_t *vid, uint16_t *pid)
{
    esp_err_t err = usb_port_ids(address, vid, pid);
    if (err == ESP_OK) {
        *index = address - 1;
        devs[*index].open = true;
        devs[*index].io.out_report_size = emu_io(&devs[*index])->out_report_size;
    }
    return err;
}

void usb_port_close(int dev)
{
    devs[dev].open = false;
}

void usb_port_string(int dev, bool serial, char *out, size_t cap)
{
    sim_dev_t *d = &devs[dev];
    if (serial) { /* Mirabox decks: the USB serial; Elgato ones answer a feature report */
        snprintf(out, cap, "MBX%04X%04X%d", d->vid, d->pid, dev);
    } else {
        snprintf(out, cap, "%s", d->elgato ? "Stream Deck" : mirabox_emu_name(dev));
    }
}

const db_hid_io_t *usb_port_io(int dev)
{
    return &devs[dev].io;
}

bool usb_port_full_speed(void)
{
    return false;
}

bool usb_port_behind_hub(int dev)
{
    return dev_count > 1;
}

const char *usb_port_path(int dev)
{
    static const char *const paths[] = {"1.1", "1.2", "1.3", "1.4"};
    return dev_count > 1 ? paths[dev] : "";
}

uint32_t usb_port_errors(void)
{
    return 0;
}

uint32_t usb_port_recoveries(void)
{
    return 0;
}

esp_err_t usb_port_stall(int dev)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t usb_port_replug(uint8_t address, uint32_t off_ms)
{
    for (int n = 0; n < dev_count; n++) {
        plug(n, false);
    }
    vTaskDelay(pdMS_TO_TICKS(off_ms));
    for (int n = 0; n < dev_count; n++) {
        plug(n, true);
    }
    return ESP_OK;
}
