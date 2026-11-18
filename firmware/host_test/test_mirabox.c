#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "db_driver.h"
#include "mirabox_ref.h"

/* A deck behind a fake io: every output report is kept. */
static uint8_t sent[32][1024];
static size_t sent_len[32];
static int sent_n;

static db_err_t rec_write(const db_hid_io_t *io, const uint8_t *r, size_t len)
{
    (void)io;
    TEST_ASSERT_LESS_THAN(32, sent_n);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(1024, len);
    memcpy(sent[sent_n], r, len);
    sent_len[sent_n++] = len;
    return DB_OK;
}

static db_err_t rec_flush(const db_hid_io_t *io)
{
    (void)io;
    return DB_OK;
}

static const db_hid_io_t io = {.write = rec_write, .flush = rec_flush};

static db_dev_t open_deck(uint16_t vid, uint16_t pid)
{
    db_dev_t dev = {.io = &io, .driver = db_driver_find(vid, pid)};
    TEST_ASSERT_NOT_NULL(dev.driver);
    TEST_ASSERT_EQUAL_STRING("mirabox", dev.driver->name);
    sent_n = 0;
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, vid, pid));
    return dev;
}

static const ref_vector_t *vec(const char *name, uint16_t packet)
{
    for (size_t i = 0; i < sizeof(ref_vectors) / sizeof(ref_vectors[0]); i++) {
        if (strcmp(ref_vectors[i].name, name) == 0 && ref_vectors[i].packet == packet) {
            return &ref_vectors[i];
        }
    }
    TEST_FAIL_MESSAGE(name);
    return NULL;
}

/* The reports sent from `from` on are those of the vector, byte for byte. */
static int expect(const char *name, uint16_t packet, int from)
{
    const ref_vector_t *v = vec(name, packet);
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(from + v->count, sent_n, name);
    for (int i = 0; i < v->count; i++) {
        uint8_t want[1024] = {0};
        memcpy(want, v->r[i].b, v->r[i].len);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(packet, sent_len[from + i], name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, sent[from + i], packet, name);
    }
    return from + v->count;
}

static const db_target_t *key_at(const db_dev_t *dev, uint8_t row, uint8_t col)
{
    const db_device_info_t *in = &dev->info;
    for (uint8_t p = 0; p < in->place_count; p++) {
        const db_place_t *pl = &in->places[p];
        for (uint8_t t = 0;
             !pl->encoder && pl->row == row && pl->col == col && t < in->displays[0].target_count; t++) {
            if (in->displays[0].targets[t].index == pl->index) {
                return &in->displays[0].targets[t];
            }
        }
    }
    return NULL;
}

static const db_place_t *place_at(const db_dev_t *dev, uint8_t row, uint8_t col)
{
    for (uint8_t p = 0; p < dev->info.place_count; p++) {
        if (dev->info.places[p].row == row && dev->info.places[p].col == col) {
            return &dev->info.places[p];
        }
    }
    return NULL;
}

static size_t input(db_dev_t *dev, uint8_t id, uint8_t state, db_input_t *out)
{
    uint8_t r[512] = {'A', 'C', 'K', 0, 0, 'O', 'K'};
    r[9] = id;
    r[10] = state;
    return dev->driver->parse_input(dev, r, sizeof(r), 42, out, 4);
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_commands_are_the_reference_bytes(void)
{
    static const struct {
        uint16_t vid, pid, packet;
    } decks[] = {{0x6603, 0x1005, 1024}, {0x5548, 0x6674, 512}};
    static const uint8_t levels[] = {0, 1, 7, 50, 99, 100};
    for (size_t d = 0; d < 2; d++) {
        uint16_t p = decks[d].packet;
        db_dev_t dev = open_deck(decks[d].vid, decks[d].pid);
        expect("led_off", p, expect("clear", p, expect("wake", p, 0)));
        TEST_ASSERT_EQUAL_INT(3, sent_n);
        sent_n = 0;
        TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->tick(&dev));
        expect("heartbeat", p, 0);
        for (size_t i = 0; i < sizeof(levels); i++) {
            char name[24];
            snprintf(name, sizeof(name), "brightness_%u", levels[i]);
            sent_n = 0;
            TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->set_brightness(&dev, levels[i]));
            TEST_ASSERT_EQUAL_INT(1, expect(name, p, 0));
        }
        const size_t sizes[] = {1, p, (size_t)p + 1, (size_t)3 * p - 5};
        const db_target_t *t = key_at(&dev, 0, 0);
        TEST_ASSERT_NOT_NULL(t);
        for (size_t i = 0; i < 4; i++) {
            static uint8_t jpeg[4096];
            for (size_t b = 0; b < sizes[i]; b++) {
                jpeg[b] = (uint8_t)(b * 7 + 3);
            }
            char name[24];
            snprintf(name, sizeof(name), "image_%zu", sizes[i]);
            sent_n = 0;
            TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->write_image(&dev, t, t->rect, jpeg, sizes[i]));
            TEST_ASSERT_EQUAL_INT(sent_n, expect(name, p, 0));
        }
    }
}

static void test_brightness_follows_the_reference_curve(void)
{
    db_dev_t dev = open_deck(0x6603, 0x1005);
    for (uint8_t b = 0; b <= 100; b++) {
        sent_n = 0;
        dev.driver->set_brightness(&dev, b);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_brightness[b], sent[0][10], "brightness");
    }
    sent_n = 0;
    dev.driver->set_brightness(&dev, 250);
    TEST_ASSERT_EQUAL_UINT8(100, sent[0][10]);
}

/* Degrees clockwise. Bitfocus' 270 for the N3 turns nothing in its code; the SDK and its AKP03E row say -90.
 */
static uint16_t rotation(int bitfocus)
{
    return bitfocus == 180 ? 180 : bitfocus == 90 ? 270 : bitfocus == 0 ? 0 : 90;
}

static void check_input(db_dev_t *dev, const ref_in_t *in, const char *msg)
{
    db_input_t ev[4];
    const db_place_t *pl = place_at(dev, in->row, in->col);
    TEST_ASSERT_NOT_NULL_MESSAGE(pl, msg);
    db_input_kind_t down = pl->encoder ? DB_INPUT_ENCODER_DOWN : DB_INPUT_KEY_DOWN;
    switch (in->kind) {
    case REF_BUTTON:
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, input(dev, in->id, 1, ev), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(down, ev[0].kind, msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, input(dev, in->id, 0, ev), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(down + 1, ev[0].kind, msg);
        break;
    case REF_PUSH:
        TEST_ASSERT_EQUAL_size_t_MESSAGE(2, input(dev, in->id, 1, ev), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(down, ev[0].kind, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(down + 1, ev[1].kind, msg);
        break;
    default:
        TEST_ASSERT_TRUE_MESSAGE(pl->encoder, msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, input(dev, in->id, 0, ev), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(DB_INPUT_ENCODER_TURN, ev[0].kind, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(in->kind == REF_LEFT || in->kind == REF_SWIPE_LEFT ? -1 : 1,
                                      ev[0].value, msg);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(pl->index, ev[0].index, msg);
}

/* Every USB id of the Bitfocus plugin: the same ids, sizes, rows and columns, and the same input. */
static void test_every_model_matches_the_reference(void)
{
    int skipped = 0;
    for (size_t m = 0; m < sizeof(ref_models) / sizeof(ref_models[0]); m++) {
        const ref_model_t *rm = &ref_models[m];
        for (uint8_t u = 0; u < rm->id_count; u++) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s %04x:%04x", rm->name, rm->ids[u].vid, rm->ids[u].pid);
            if (!db_driver_find(rm->ids[u].vid, rm->ids[u].pid)) {
                /* their sizes differ from Mirabox's SDK */
                TEST_ASSERT_TRUE_MESSAGE(strstr(rm->name, "M18") || strstr(rm->name, "N1"), msg);
                skipped++;
                continue;
            }
            db_dev_t dev = open_deck(rm->ids[u].vid, rm->ids[u].pid);
            const db_display_t *d = &dev.info.displays[0];
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(rotation(rm->rotation), d->rotate, msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(rm->out_count, d->target_count, msg);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(rm->packet, sent_len[0], msg);
            TEST_ASSERT_FALSE_MESSAGE(dev.info.dock, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("streamdock", dev.info.surface, msg);
            for (uint8_t o = 0; o < rm->out_count; o++) {
                const ref_out_t *ro = &rm->out[o];
                const db_target_t *t = key_at(&dev, ro->row, ro->col);
                TEST_ASSERT_NOT_NULL_MESSAGE(t, msg);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE(ro->w, t->rect.w, msg);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE(ro->h, t->rect.h, msg);
                TEST_ASSERT_TRUE_MESSAGE(
                    t->rect.x + t->rect.w <= d->width && t->rect.y + t->rect.h <= d->height, msg);
                static const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
                sent_n = 0;
                TEST_ASSERT_EQUAL_INT_MESSAGE(DB_OK, dev.driver->write_image(&dev, t, t->rect, jpeg, 4), msg);
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE("CRT\0\0BAT\0\0\0\4", sent[0], 12, msg);
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(ro->id, sent[0][12], msg);
            }
            for (uint8_t i = 0; i < rm->in_count; i++) {
                check_input(&dev, &rm->in[i], msg);
            }
            for (uint8_t p = 0; p < dev.info.place_count; p++) {
                bool known = false;
                for (uint8_t i = 0; i < rm->in_count; i++) {
                    known |=
                        rm->in[i].row == dev.info.places[p].row && rm->in[i].col == dev.info.places[p].col;
                }
                for (uint8_t o = 0; o < rm->out_count; o++) {
                    known |=
                        rm->out[o].row == dev.info.places[p].row && rm->out[o].col == dev.info.places[p].col;
                }
                TEST_ASSERT_TRUE_MESSAGE(known, msg);
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(3, skipped);
}

/* What the web UI simulates comes back from parse_input as that input. */
static void test_built_input_parses_back(void)
{
    db_dev_t dev = open_deck(0x6602, 0x1001);
    uint8_t r[65];
    db_input_t ev[4];
    for (uint8_t k = 0; k < dev.info.key_count; k++) {
        size_t len =
            dev.driver->build_input(&dev, &(db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = k}, r, 65);
        TEST_ASSERT_GREATER_THAN(0, len);
        size_t n = dev.driver->parse_input(&dev, r, len, 0, ev, 4);
        TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_DOWN, ev[0].kind);
        TEST_ASSERT_EQUAL_UINT8(k, ev[0].index);
        /* the zones under the strip report a press only; their release comes with it */
        len = dev.driver->build_input(&dev, &(db_input_t){.kind = DB_INPUT_KEY_UP, .index = k}, r, 65);
        TEST_ASSERT_EQUAL_size_t(n == 2 ? 0 : 11, len);
    }
    for (uint8_t e = 0; e < dev.info.encoder_count; e++) {
        size_t len = dev.driver->build_input(
            &dev, &(db_input_t){.kind = DB_INPUT_ENCODER_TURN, .index = e, .value = -3}, r, 65);
        TEST_ASSERT_EQUAL_size_t(1, dev.driver->parse_input(&dev, r, len, 0, ev, 4));
        TEST_ASSERT_EQUAL_INT(-1, ev[0].value);
        TEST_ASSERT_EQUAL_UINT8(e, ev[0].index);
    }
    TEST_ASSERT_EQUAL_size_t(
        0, dev.driver->build_input(&dev, &(db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = 14}, r, 65));
    TEST_ASSERT_EQUAL_size_t(0,
                             dev.driver->build_input(&dev, &(db_input_t){.kind = DB_INPUT_TOUCH_TAP}, r, 65));
}

static void test_reports_that_are_no_input_give_nothing(void)
{
    db_dev_t dev = open_deck(0x6603, 0x1005);
    db_input_t ev[4];
    uint8_t r[512] = {'A', 'C', 'K', 0, 0, 'O', 'K', 0, 0, 0x01, 0x01};
    TEST_ASSERT_EQUAL_size_t(1, dev.driver->parse_input(&dev, r, sizeof(r), 0, ev, 4));
    TEST_ASSERT_EQUAL_size_t(0, dev.driver->parse_input(&dev, r, 10, 0, ev, 4));
    r[9] = 0xff; /* a write was taken */
    TEST_ASSERT_EQUAL_size_t(0, dev.driver->parse_input(&dev, r, sizeof(r), 0, ev, 4));
    r[9] = 0x01;
    r[0] = 'X';
    TEST_ASSERT_EQUAL_size_t(0, dev.driver->parse_input(&dev, r, sizeof(r), 0, ev, 4));
}

static void test_an_image_over_10_kb_is_refused(void)
{
    db_dev_t dev = open_deck(0x6603, 0x1005);
    static uint8_t big[10241];
    const db_target_t *t = key_at(&dev, 1, 1);
    TEST_ASSERT_EQUAL_UINT16(10240, dev.info.displays[0].max_bytes);
    sent_n = 0;
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, dev.driver->write_image(&dev, t, t->rect, big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT(0, sent_n);
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->write_image(&dev, t, t->rect, big, 10240));
    TEST_ASSERT_EQUAL_INT(1 + 10 + 1, sent_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_commands_are_the_reference_bytes);
    RUN_TEST(test_brightness_follows_the_reference_curve);
    RUN_TEST(test_every_model_matches_the_reference);
    RUN_TEST(test_built_input_parses_back);
    RUN_TEST(test_reports_that_are_no_input_give_nothing);
    RUN_TEST(test_an_image_over_10_kb_is_refused);
    return UNITY_END();
}
