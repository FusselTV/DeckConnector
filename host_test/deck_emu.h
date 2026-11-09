#pragma once

#include "db_driver.h"

/*
 * A virtual Stream Deck behind a db_hid_io_t, built only from Elgato's HID pages.
 * It answers feature reports like the real deck, checks every report a driver sends and records each
 * protocol violation. The io callbacks carry no context, so there is one current deck at a time.
 */

typedef enum { DECK_CLASSIC, DECK_XL, DECK_NEO, DECK_PLUS, DECK_SIXKEY, DECK_PEDAL } deck_family_t;

typedef struct {
    uint16_t pid;
    const char *name;
    deck_family_t family;
    uint8_t cols;
    uint8_t rows;
    uint8_t sensors; /* input-only keys after the image keys (Neo) */
    uint16_t key_px; /* key images are square */
    uint16_t lcd_w, lcd_h;
    uint16_t win_w, win_h; /* 0 without a window */
    bool touch;
    uint8_t encoders;
    uint16_t rotate;  /* degrees clockwise the image is rotated before upload */
    uint8_t tap_len;  /* payload length of a touch tap report */
} deck_model_t;

typedef struct {
    uint8_t cmd; /* gen2 0x07 0x08 0x0b 0x0c, six-key 0x01 */
    uint8_t key; /* six-key: header byte minus one */
    db_rect_t win; /* 0x0c header rect, window coordinates */
    uint16_t img_w, img_h; /* from the JPEG SOF0 or BMP header */
    uint8_t *data;
    size_t len;
    size_t first_report; /* index for deck_emu_report() */
    size_t report_count;
} deck_image_t;

extern const deck_model_t deck_models[];
extern const size_t deck_model_count;
const deck_model_t *deck_model(uint16_t pid);

bool deck_emu_start(uint16_t pid);
const db_hid_io_t *deck_emu_io(void);
/* Forget violations, images, reports and brightness. Key and encoder states stay. */
void deck_emu_clear(void);
/* Records a violation if an image transfer is still waiting for its last chunk. */
void deck_emu_check_idle(void);

size_t deck_emu_violation_count(void);
const char *deck_emu_violation(size_t i);
size_t deck_emu_image_count(void);
const deck_image_t *deck_emu_image(size_t i);
size_t deck_emu_report_count(void);
const uint8_t *deck_emu_report(size_t i); /* 1024 bytes, zero padded */
int deck_emu_brightness(void);            /* -1 until set */
const char *deck_emu_serial(void);
const char *deck_emu_firmware(void);

/* Input reports the deck sends. They return the report length, 0 when the deck has no such input. */
size_t deck_emu_key(uint8_t key, bool pressed, uint8_t *out, size_t cap);
size_t deck_emu_encoder_press(uint8_t enc, bool pressed, uint8_t *out, size_t cap);
size_t deck_emu_encoder_turn(uint8_t enc, int8_t ticks, uint8_t *out, size_t cap);
/* kind 1 tap, 2 long press, 3 flick; window coordinates */
size_t deck_emu_touch(uint8_t kind, uint16_t x, uint16_t y, uint16_t x2, uint16_t y2, uint8_t *out, size_t cap);
