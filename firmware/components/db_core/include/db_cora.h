#pragma once

#include "db_device.h"

/*
 * "Cora", the TCP framing of the Elgato Network Dock: a 16 byte header, then a HID report.
 * Header: magic 43 93 8a 41, u16 flags, u8 hid op, u8 reserved, u32 message id, u32 payload length.
 */

#define DB_CORA_HDR_LEN     16
#define DB_CORA_REPORT_LEN  512
#define DB_CORA_DOCK_PORT   5343
#define DB_CORA_DEVICE_PORT 5344
#define DB_CORA_FIRMWARE    "1.01.014" /* what we report as dock firmware */

enum {
    DB_CORA_FLAG_VERBATIM = 0x8000, /* payload belongs to the attached device */
    DB_CORA_FLAG_RESULT = 0x0100,
};

enum {
    DB_CORA_OP_WRITE = 0,       /* output report */
    DB_CORA_OP_SEND_REPORT = 1, /* set feature report */
    DB_CORA_OP_GET_REPORT = 2,  /* get feature report */
};

typedef struct {
    uint16_t flags;
    uint8_t op;
    uint32_t id;
    uint32_t len;
} db_cora_hdr_t;

/* DB_OK with a header, DB_ERR_STATE when fewer than 16 bytes, DB_ERR_PROTO when the magic is wrong. */
db_err_t db_cora_parse_hdr(const uint8_t *buf, size_t len, db_cora_hdr_t *out);
void db_cora_write_hdr(uint8_t out[DB_CORA_HDR_LEN], const db_cora_hdr_t *h);

typedef struct {
    char serial[16]; /* of the dock itself */
    uint8_t mac[6];
    const db_device_info_t *device; /* NULL: nothing attached */
} db_cora_dock_t;

/*
 * Answer to a GET_REPORT [0x03, cmd] on the dock port, DB_CORA_REPORT_LEN bytes.
 * 0x80 identity, 0x83 firmware, 0x84 serial, 0x85 MAC, 0x87 unknown, 0x1c attached device. Others get zeros.
 */
void db_cora_dock_report(uint8_t cmd, const db_cora_dock_t *dock, uint8_t out[DB_CORA_REPORT_LEN]);

/* The dock's periodic keepalive; hosts drop the connection after 5 s without data. */
void db_cora_keepalive(uint8_t connection, uint8_t out[DB_CORA_REPORT_LEN]);
