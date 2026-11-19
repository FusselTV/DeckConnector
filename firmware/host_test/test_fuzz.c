/*
 * Malformed wire input into every parser a host or Companion reaches: Satellite lines, PNG and BMP pictures,
 * Cora headers, server frames and dock image reports through each driver. Mutated seeds with a fixed seed;
 * ASan and UBSan do the checking. FUZZ_ROUNDS=n runs longer; with -DFUZZING -fsanitize=fuzzer it is a
 * libFuzzer target.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "db_cora.h"
#include "db_dns.h"
#include "db_image.h"
#include "db_proto.h"
#include "db_quirks.h"
#include "db_sat.h"
#include "deck_emu.h"

size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src, size_t src_len, int flags)
{
    (void)flags;
    uLongf n = out_len;
    return uncompress(out, &n, src, src_len) == Z_OK ? (size_t)n : (size_t)-1;
}

static void sat(const uint8_t *d, size_t n)
{
    char *line = malloc(n + 1);
    memcpy(line, d, n);
    line[n] = '\0';
    db_sat_msg_t m;
    db_sat_parse(line, &m);
    db_sat_lock_t l = {0};
    db_sat_lock_update(&l, &m);
    const db_sat_control_t c[] = {{.row = 0, .col = 1}, {.kind = DB_SAT_LCD, .rect = {0, 0, 100, 10}}};
    const char *id = db_sat_get(&m, "CONTROLID");
    if (id) {
        db_sat_find(c, 2, id);
    }
    char host[64];
    db_sat_addr_host(line, host, sizeof(host));
    free(line);
}

static void png(const uint8_t *d, size_t n)
{
    if (n < 2) {
        return;
    }
    uint16_t w = d[0] % 200;
    uint16_t h = d[1] % 200;
    size_t cap = (size_t)w * h * 4 + 1024;
    uint8_t *bgr = malloc((size_t)w * h * 3 + 1);
    uint8_t *work = malloc(cap);
    db_png_decode(d + 2, n - 2, w, h, bgr, work, cap);
    free(bgr);
    free(work);
}

static void bmp(const uint8_t *d, size_t n)
{
    if (n < 2) {
        return;
    }
    uint16_t w = d[0] % 100;
    uint16_t h = d[1] % 100;
    uint8_t *px = malloc((size_t)w * h * 3 + 1);
    db_bmp_read(d + 2, n - 2, w, h, px);
    free(px);
}

static void frames(const uint8_t *d, size_t n)
{
    db_cora_hdr_t h;
    if (db_cora_parse_hdr(d, n, &h) == DB_OK && n > DB_CORA_HDR_LEN) {
        db_quirk(db_get_u16(d + 4), h.op, d + DB_CORA_HDR_LEN, n - DB_CORA_HDR_LEN);
        db_device_info_t dev = {.pid = 0x0084};
        db_cora_dock_t dock = {.device = n % 2 ? &dev : NULL};
        uint8_t out[DB_CORA_REPORT_LEN];
        db_cora_dock_report(d[DB_CORA_HDR_LEN], &dock, out);
    }
    db_image_hdr_t img;
    uint64_t t;
    db_proto_parse_image_hdr(d, n, &img);
    db_proto_parse_time(d, n, &t);
    char name[DB_DNS_NAME_LEN];
    if (n > 2) {
        db_dns_ptr_name(d + 2, n - 2, db_get_u16(d), name);
    }
}

/* Reports a dock host writes to the device port, through the deck's driver into the kept picture. */
static void dock(const uint8_t *d, size_t n)
{
    if (n < 2) {
        return;
    }
    uint16_t pid = deck_models[d[0] % deck_model_count].pid;
    db_dev_t dev = {.io = deck_emu_io(), .driver = db_driver_find(0x0fd9, pid)};
    if (!deck_emu_start(pid) || !dev.driver || dev.driver->open(&dev, 0x0fd9, pid) != DB_OK) {
        return;
    }
    db_screen_t sc;
    db_screen_init(&sc, realloc, free);
    size_t size = dev.io->out_report_size;
    uint8_t *r = malloc(size);
    for (size_t at = 1; at < n;) {
        size_t len = d[at] % 2 ? size : d[at];
        at++;
        size_t take = n - at < len ? n - at : len;
        memset(r, 0, size);
        memcpy(r, d + at, take);
        at += take;
        db_chunk_t c;
        if (dev.driver->parse_output(&dev, r, size, &c) &&
            db_device_find_target(&dev.info, c.display, c.rect)) {
            db_screen_feed(&sc, &c);
        }
        db_input_t ev[32];
        dev.driver->parse_input(&dev, r, take, 0, ev, 32);
    }
    db_screen_clear(&sc);
    free(r);
    deck_emu_clear();
}

static void (*const targets[])(const uint8_t *, size_t) = {sat, png, bmp, frames, dock};
#define TARGETS (sizeof(targets) / sizeof(targets[0]))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size) {
        targets[data[0] % TARGETS](data + 1, size - 1);
    }
    return 0;
}

#ifndef FUZZING
static uint32_t rng = 1;

static uint32_t rnd(uint32_t n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}

static size_t add(uint8_t *out, const void *p, size_t n)
{
    memcpy(out, p, n);
    return n;
}

static size_t png_seed(uint8_t *out, uint8_t w, uint8_t h, uint8_t type)
{
    static uint8_t raw[16 * (1 + 4 * 16)];
    size_t bpp = type == 6 ? 4 : 3;
    size_t rlen = (size_t)h * (1 + bpp * w);
    for (size_t i = 0; i < rlen; i++) {
        raw[i] = i % (1 + bpp * w) ? (uint8_t)(i * 7) : (uint8_t)(i % 5);
    }
    uint8_t z[2048];
    uLongf zlen = sizeof(z);
    compress(z, &zlen, raw, rlen);
    size_t n = add(out, (uint8_t[]){0, w, h, 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}, 11);
    const uint8_t ihdr[] = {0, 0, 0, 13, 'I',  'H', 'D', 'R', 0, 0, 0, w, 0,
                            0, 0, h, 8,  type, 0,   0,   0,   0, 0, 0, 0};
    n += add(out + n, ihdr, sizeof(ihdr));
    size_t half = zlen / 2;
    for (int part = 0; part < 2; part++) { /* two IDAT chunks, so the joining runs too */
        size_t len = part ? zlen - half : half;
        uint8_t head[8] = {0, 0, (uint8_t)(len >> 8), (uint8_t)len, 'I', 'D', 'A', 'T'};
        n += add(out + n, head, 8);
        n += add(out + n, z + (part ? half : 0), len);
        n += add(out + n, (uint8_t[4]){0}, 4);
    }
    return n;
}

int main(void)
{
    static uint8_t seeds[16][4096];
    size_t lens[16];
    int k = 0;
#define SEED(...) lens[k] = add(seeds[k], (uint8_t[]){__VA_ARGS__}, sizeof((uint8_t[]){__VA_ARGS__})), k++
#define SEED_STR(t, s) seeds[k][0] = (t), lens[k] = 1 + add(seeds[k] + 1, s, strlen(s)), k++
    SEED_STR(0,
             "KEY-STATE DEVICEID=\"A1\" CONTROLID=\"1/2\" BITMAP=\"AAAA\" BITMAP_ID=\"x\\\"y\" PRESSED=true");
    SEED_STR(0, "LOCKED-STATE DEVICEID=A1 LOCKED=true CHARACTER_COUNT=999 ROTATION=-90 CONTROLID=/0");
    SEED_STR(0, "wss://host:99999/x");
    lens[k] = png_seed(seeds[k], 16, 9, 2), seeds[k][0] = 1, k++;
    lens[k] = png_seed(seeds[k], 5, 7, 6), seeds[k][0] = 1, k++;
    seeds[k][0] = 2, seeds[k][1] = seeds[k][2] = 2;
    db_bmp_write((const uint8_t[12]){1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 2, 2, seeds[k] + 3);
    lens[k] = 3 + db_bmp_size(2, 2), k++;
    SEED(3, 0x43, 0x93, 0x8a, 0x41, 0x80, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 2, 3, 0x1c);
    SEED(3, 1, 0, 0, 0, 0, 0, 72, 0, 72, 0, 0, 0, 0, 1, 2, 3);
    SEED(3, 0x34, 0x12, 0x12, 0x34, 0x81, 0x80, 0, 1, 0, 1, 0, 0, 0, 0, 1, '4', 0, 0, 12, 0, 1, 0xc0, 12, 0,
         12, 0, 1, 0, 0, 0, 60, 0, 5, 3, 'p', 'c', '1', 0);
    for (uint8_t m = 0; m < 6; m++) {
        SEED(4, m, 1, 2, 7, 3, 1, 0x40, 0, 0, 0, 0, 1, 0, 2, 0x0c, 0, 0, 0, 0, 0, 0x20, 0, 0x20, 0, 1, 0, 0,
             0x40, 0);
    }
    const char *env = getenv("FUZZ_ROUNDS");
    long rounds = env ? atol(env) : 20000;
    static uint8_t buf[8192];
    for (long i = 0; i < rounds; i++) {
        int s = (int)rnd((uint32_t)k);
        size_t n = lens[s];
        memcpy(buf, seeds[s], n);
        for (uint32_t edits = 1 + rnd(8); edits > 0; edits--) {
            static const uint8_t odd[] = {0, 1, 0x7f, 0x80, 0xfe, 0xff, '"', '\\', ' ', '=', '/', '\n'};
            uint32_t at = 1 + rnd((uint32_t)(n > 1 ? n - 1 : 1));
            switch (rnd(6)) {
            case 0:
                buf[at] ^= (uint8_t)(1u << rnd(8));
                break;
            case 1:
                buf[at] = odd[rnd(sizeof(odd))];
                break;
            case 2:
                n = at;
                break;
            case 3:
                for (uint32_t extra = rnd(600); extra > 0 && n < sizeof(buf); extra--) {
                    buf[n++] = (uint8_t)rnd(256);
                }
                break;
            case 4:
                buf[at] = (uint8_t)rnd(256);
                break;
            default:
                if (n * 2 <= sizeof(buf)) {
                    memcpy(buf + n, buf + 1, n - 1);
                    n += n - 1;
                }
                break;
            }
        }
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("%ld inputs\n", rounds);
    return 0;
}
#endif
