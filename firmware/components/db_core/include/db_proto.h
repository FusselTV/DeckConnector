#pragma once

#include "db_device.h"

/* Binary and JSON encoding of the server protocol. See https://deckconnector.com/docs/own-server. */

#define DB_PROTO_VERSION 1

enum {
    DB_MSG_IMAGE = 0x01,
    DB_MSG_TIME = 0x02,
    DB_MSG_ACK = 0x81,
    DB_MSG_TIME_REPLY = 0x82,
    DB_MSG_INPUT = 0x83,
};

#define DB_IMAGE_HDR_LEN  14
#define DB_ACK_LEN        32
#define DB_TIME_LEN       16
#define DB_TIME_REPLY_LEN 24
#define DB_INPUT_LEN      32

typedef enum {
    DB_ACK_SHOWN = 0,
    DB_ACK_SUPERSEDED = 1,
    DB_ACK_REJECTED_RECT = 2,
    DB_ACK_REJECTED_ROLE = 3,
    DB_ACK_NO_DEVICE = 4,
    DB_ACK_BUSY = 5,
} db_ack_result_t;

typedef struct {
    uint8_t display;
    db_rect_t rect;
    uint32_t seq;
} db_image_hdr_t;

db_err_t db_proto_parse_image_hdr(const uint8_t *buf, size_t len, db_image_hdr_t *out);
db_err_t db_proto_parse_time(const uint8_t *buf, size_t len, uint64_t *server_time_us);

void db_proto_write_ack(uint8_t out[DB_ACK_LEN], db_ack_result_t result, uint8_t display, uint32_t seq,
                        uint64_t t_recv, uint64_t t_start, uint64_t t_done);
void db_proto_write_time_reply(uint8_t out[DB_TIME_REPLY_LEN], uint64_t server_time_us,
                               uint64_t bridge_time_us);
void db_proto_write_input(uint8_t out[DB_INPUT_LEN], const db_input_t *ev);

/*
 * Write the `device` JSON message. `info == NULL` writes the unplugged form. Returns the length without the
 * terminating NUL, or 0 when `cap` is too small.
 */
size_t db_proto_device_json(const db_device_info_t *info, char *buf, size_t cap);
