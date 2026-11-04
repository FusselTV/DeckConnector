#include "db_cora.h"

#include <string.h>

/* Network Dock protocol as Companion (@elgato-stream-deck/tcp) and the Elgato App expect it. */

static const uint8_t magic[4] = {0x43, 0x93, 0x8a, 0x41};

db_err_t db_cora_parse_hdr(const uint8_t *buf, size_t len, db_cora_hdr_t *out)
{
    if (len < DB_CORA_HDR_LEN) {
        return DB_ERR_STATE;
    }
    if (memcmp(buf, magic, sizeof(magic)) != 0) {
        return DB_ERR_PROTO;
    }
    *out = (db_cora_hdr_t){
        .flags = db_get_u16(buf + 4), .op = buf[6], .id = db_get_u32(buf + 8), .len = db_get_u32(buf + 12)};
    return DB_OK;
}

void db_cora_write_hdr(uint8_t out[DB_CORA_HDR_LEN], const db_cora_hdr_t *h)
{
    memcpy(out, magic, sizeof(magic));
    db_put_u16(out + 4, h->flags);
    out[6] = h->op;
    out[7] = 0;
    db_put_u32(out + 8, h->id);
    db_put_u32(out + 12, h->len);
}

static void device2_info(const db_cora_dock_t *dock, uint8_t *out)
{
    out[0] = 0x01;
    out[1] = 0x0b;
    out[2] = 0x7c;
    out[4] = dock->device ? 0x02 : 0x01; /* 0x02: attached and working */
    if (!dock->device) {
        return;
    }
    db_put_u16(out + 26, dock->device->vid);
    db_put_u16(out + 28, dock->device->pid);
    size_t n = strnlen(dock->device->serial, 31);
    memcpy(out + 94, dock->device->serial, n);
    db_put_u16(out + 126, DB_CORA_DEVICE_PORT);
}

/* Fixed answers, byte for byte as the Stream Deck app expects them. It checks 0x81 to 0x8f.
 */
static const struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t bytes[16];
} fixed[] = {
    {0x81, 16, {0x03, 0x81, 0, 0, 0x65, 0x5b, 0x80, 0x4c, '1', '.', '0', '0', '.', '0', '0', '0'}},
    {0x82, 16, {0x03, 0x82, 0, 0, 0x62, 0x85, 0x62, 0x35, '1', '.', '0', '1', '.', '0', '1', '3'}},
    {0x83, 16, {0x03, 0x83, 0, 0, 0x25, 0xe4, 0x90, 0x96, '1', '.', '0', '1', '.', '0', '1', '4'}},
    {0x87, 6, {0x03, 0x87, 0, 0x02, 0, 0x02}},
    {0x88, 4, {0x03, 0x88, 0, 0x08}},
    {0x8b, 10, {0x03, 0x8b, 0x08, 0, 0, 0, 0, 0, 0x43, 0x0e}},
    {0x8e, 2, {0x03, 0x8e}},
};

static int find_fixed(uint8_t cmd)
{
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (fixed[i].cmd == cmd) {
            return (int)i;
        }
    }
    return -1;
}

bool db_cora_is_query(uint8_t cmd)
{
    return cmd == 0x80 || cmd == 0x84 || cmd == 0x85 || cmd == 0x8f || cmd == 0x1c || find_fixed(cmd) >= 0;
}

void db_cora_dock_report(uint8_t cmd, const db_cora_dock_t *dock, uint8_t out[DB_CORA_REPORT_LEN])
{
    memset(out, 0, DB_CORA_REPORT_LEN);
    if (cmd == 0x1c) {
        device2_info(dock, out);
        return;
    }
    int f = find_fixed(cmd);
    if (f >= 0) {
        memcpy(out, fixed[f].bytes, fixed[f].len);
        return;
    }
    out[0] = 0x03;
    out[1] = cmd;
    switch (cmd) {
    case 0x80: /* identity: vendor and the pseudo product id of the dock */
        db_put_u16(out + 12, 0x0fd9);
        db_put_u16(out + 14, 0xffff);
        break;
    case 0x84: {
        size_t n = strnlen(dock->serial, sizeof(dock->serial));
        out[3] = (uint8_t)n;
        memcpy(out + 4, dock->serial, n);
        break;
    }
    case 0x85:
        out[2] = 6;
        memcpy(out + 4, dock->mac, 6);
        break;
    case 0x8f: /* busy state; a dock in use by a host answers 03 8f 02 00 02 */
        out[2] = dock->in_use ? 0x02 : 0x00;
        out[4] = dock->in_use ? 0x02 : 0x00;
        break;
    default:
        break;
    }
}

void db_cora_keepalive(uint8_t connection, uint8_t out[DB_CORA_REPORT_LEN])
{
    memset(out, 0, DB_CORA_REPORT_LEN);
    out[0] = 0x01;
    out[1] = 0x0a;
    out[2] = 0x02;
    out[4] = 0x02;
    out[5] = connection;
}
