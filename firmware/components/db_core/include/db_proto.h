#pragma once

#include "db_device.h"

/* Encoding of the surface protocol v2: binary messages with a 4 byte header, CBOR for control messages. See
 * https://deckconnector.com/docs/own-server. */

#define DB_PROTO_VERSION   2
#define DB_PROTO_HDR       4
#define DB_PROTO_BOARD     0xff /* the deck byte of a message for the whole board */
#define DB_PROTO_IMAGE_MAX (1024 * 1024)

enum {
    DB_MSG_ROLE = 0x01,
    DB_MSG_PUT = 0x02,
    DB_MSG_EVICT = 0x03,
    DB_MSG_FRAME = 0x04,
    DB_MSG_STREAM = 0x05,
    DB_MSG_FALLBACK = 0x06,
    DB_MSG_BRIGHTNESS = 0x07,
    DB_MSG_TIME = 0x08,
    DB_MSG_REBOOT = 0x09,
    DB_MSG_PAIRED = 0x0a,
    DB_MSG_HELLO = 0x80,
    DB_MSG_SHOWN = 0x81,
    DB_MSG_CREDIT = 0x82,
    DB_MSG_INPUT = 0x83,
    DB_MSG_DECK_ADDED = 0x84,
    DB_MSG_DECK_REMOVED = 0x85,
    DB_MSG_STATE = 0x86,
    DB_MSG_ERROR = 0x87,
};

enum { DB_IMG_JPEG = 0, DB_IMG_BMP = 1, DB_IMG_PNG = 2 };

#define DB_SHOWN_LEN  (DB_PROTO_HDR + 9)
#define DB_CREDIT_LEN (DB_PROTO_HDR + 6)
#define DB_INPUT_LEN  (DB_PROTO_HDR + 24)

/* What became of one image handed to the deck service. */
typedef enum {
    DB_ACK_SHOWN = 0,
    DB_ACK_SUPERSEDED = 1,
    DB_ACK_REJECTED_RECT = 2,
    DB_ACK_REJECTED_ROLE = 3,
    DB_ACK_NO_DEVICE = 4,
    DB_ACK_BUSY = 5,
    DB_ACK_TOO_BIG = 6,
} db_ack_result_t;

void db_proto_hdr(uint8_t *out, uint8_t type, uint8_t deck);
void db_proto_shown(uint8_t out[DB_SHOWN_LEN], uint8_t deck, bool stream, uint32_t id, uint32_t us);
void db_proto_credit(uint8_t out[DB_CREDIT_LEN], uint8_t deck, uint8_t display, uint32_t bytes,
                     uint8_t frames);
void db_proto_input(uint8_t out[DB_INPUT_LEN], uint8_t deck, uint32_t seq, uint64_t usb_time_us,
                    const db_input_t *ev);

/* Target `id` as HELLO numbers them: keys by their number, then strips, then whole displays. NULL when there
 * is none. */
const db_target_t *db_proto_target(const db_device_info_t *info, uint8_t id, uint8_t *display);
bool db_proto_format_ok(const db_display_t *d, uint8_t format);

/* CBOR with indefinite maps and arrays. `key` is NULL for an item of an array. */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool over;
} db_cbor_t;

void db_cbor_begin(db_cbor_t *c, uint8_t *buf, size_t cap);
void db_cbor_map(db_cbor_t *c, const char *key);
void db_cbor_arr(db_cbor_t *c, const char *key);
void db_cbor_close(db_cbor_t *c);
void db_cbor_str(db_cbor_t *c, const char *key, const char *v);
void db_cbor_uint(db_cbor_t *c, const char *key, uint64_t v);
void db_cbor_bool(db_cbor_t *c, const char *key, bool v);
/* The length, 0 when it did not fit. */
size_t db_cbor_end(db_cbor_t *c);

/* The text under `key` in a CBOR map with text keys. False when it is missing, no text or does not fit. */
bool db_cbor_text(const uint8_t *buf, size_t len, const char *key, char *out, size_t cap);

/* One entry of HELLO.decks, the body of DECK_ADDED. */
void db_proto_deck(db_cbor_t *c, const char *key, int deck, const db_device_info_t *info);
