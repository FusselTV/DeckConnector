#pragma once

#include "db_device.h"

/*
 * The parts of the Bitfocus Companion Satellite API (TCP 16622) that need no network: reading a line, the
 * layout of a deck, and PNG pictures. The client is db_link/sat.c, the user docs
 * https://deckconnector.com/docs/companion.
 */

#define DB_SAT_PORT         16622
#define DB_SAT_MAX_PARAMS   16
#define DB_SAT_MAX_CONTROLS (DB_MAX_TARGETS + 8)

typedef struct {
    const char *key;
    const char *value; /* "" for a flag without a value */
} db_sat_param_t;

typedef struct {
    const char *cmd;
    int count;
    db_sat_param_t p[DB_SAT_MAX_PARAMS];
} db_sat_msg_t;

/* Split `line` (without its newline) in place into the command and its KEY=value pairs, with quotes and
 * backslashes as Companion writes them. Pairs beyond DB_SAT_MAX_PARAMS are dropped. */
void db_sat_parse(char *line, db_sat_msg_t *out);
/* NULL when `key` is missing. */
const char *db_sat_get(const db_sat_msg_t *m, const char *key);
/* The host of a `satellite` setting ("host", "host:port", ws:// or wss:// URL), "" for "". */
void db_sat_addr_host(const char *addr, char *host, size_t cap);

typedef enum {
    DB_SAT_KEY,
    DB_SAT_LCD, /* one part of the LCD strip */
    DB_SAT_ENCODER,
} db_sat_kind_t;

typedef struct {
    db_sat_kind_t kind;
    uint8_t row;
    uint8_t col;
    uint8_t index; /* key or encoder index */
    db_rect_t rect;
} db_sat_control_t;

/* Rows and columns as Companion's deck plugin lays out the deck over USB or a Network Dock, so the
 * same buttons show. The image keys come first, then the strip in one part per encoder, then keys without a
 * picture (on the Neo, in the strip's row around it), then the encoders. */
size_t db_sat_controls(const db_device_info_t *dev, db_sat_control_t *out, size_t max);
/* The LAYOUT_MANIFEST JSON. Returns its length, 0 when it does not fit. */
size_t db_sat_manifest(const db_sat_control_t *c, size_t n, char *buf, size_t cap);
/* The control of a CONTROLID "row/column", or NULL. */
const db_sat_control_t *db_sat_find(const db_sat_control_t *c, size_t n, const char *id);
/* The strip part under touch position `x`, or NULL. */
const db_sat_control_t *db_sat_lcd_at(const db_sat_control_t *c, size_t n, uint16_t x);

/*
 * An 8 bit RGB or RGBA PNG without interlacing and exactly w x h into B, G, R bytes, the order the JPEG
 * encoder reads. Alpha is dropped. `work` holds the inflated rows, h * (1 + 4 * w) bytes.
 */
db_err_t db_png_decode(const uint8_t *png, size_t len, uint16_t w, uint16_t h, uint8_t *bgr, uint8_t *work,
                       size_t work_cap);
