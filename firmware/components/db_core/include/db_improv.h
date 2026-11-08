#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Improv Wi-Fi Serial framing, https://www.improv-wifi.com/serial/ */

enum { DB_IMPROV_STATE = 1, DB_IMPROV_ERROR = 2, DB_IMPROV_RPC = 3, DB_IMPROV_RESULT = 4 };

/* The largest frame: newline, header, version, type, length, 255 data bytes, checksum, newline. */
#define DB_IMPROV_FRAME_MAX (1 + 6 + 3 + 255 + 1 + 1)

typedef struct {
    uint8_t buf[6 + 3 + 255];
    size_t got;
} db_improv_parser_t;

typedef struct {
    uint8_t type;
    uint8_t len;
    const uint8_t *data;
} db_improv_packet_t;

typedef enum { DB_IMPROV_MORE, DB_IMPROV_PACKET, DB_IMPROV_BAD_CHECKSUM } db_improv_feed_t;

/* One byte from the line. Bytes outside a packet (log lines, newlines) are skipped. `out` points into the
 * parser and stays valid until the next call. */
db_improv_feed_t db_improv_feed(db_improv_parser_t *p, uint8_t byte, db_improv_packet_t *out);

/* The frame of a packet into `out` (DB_IMPROV_FRAME_MAX bytes), its length returned. A newline goes before
 * and after it: a reader that collects lines finds the header at the start of one even when a log line was
 * cut. */
size_t db_improv_frame(uint8_t *out, uint8_t type, const uint8_t *data, uint8_t len);

/* An RPC packet's command and its data; false when its lengths do not add up. */
bool db_improv_rpc(const db_improv_packet_t *p, uint8_t *cmd, const uint8_t **data, uint8_t *len);

/* The length-prefixed string at `*pos` as a C string, `*pos` moved past it. False when it runs past `len`,
 * does not fit `cap` or holds a NUL byte. */
bool db_improv_str(const uint8_t *data, uint8_t len, size_t *pos, char *out, size_t cap);

/* RPC result data (command, length, the strings with length prefix) into `out` (255 bytes); 0 when it does
 * not fit. */
size_t db_improv_result(uint8_t *out, uint8_t cmd, const char *const *strs, size_t n);
