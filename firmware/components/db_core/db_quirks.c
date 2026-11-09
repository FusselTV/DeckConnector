#include "db_quirks.h"

#include "db_cora.h"

static bool six_key(uint16_t pid)
{
    return pid == 0x0063 || pid == 0x0090 || pid == 0x00b3 || pid == 0x00b8;
}

static uint32_t after_write(uint16_t pid, const uint8_t *p)
{
    if (pid == 0x0086 && p[0] == 0x02) { /* the Pedal's own firmware update */
        switch (p[1]) {
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0d:
        case 0x0f:
            return 280;
        case 0x05:
            return p[3] && p[4] ? 900 : 0;
        default:
            return 10;
        }
    }
    if (six_key(pid)) {
        return p[1] == 0x02 ? (pid == 0x0090 ? 2 : p[3] ? 12 : 1100) : 0;
    }
    if (p[0] == 0x02 && p[1] == 0x09 && p[3]) { /* a deck firmware update restarts the deck */
        return pid == 0x006c || pid == 0x0084 ? 4200 : 2000;
    }
    bool sleeps_after_08 =
        pid == 0x006d || pid == 0x0080 || pid == 0x00a5 || pid == 0x00b9 || pid == 0x009a || pid == 0x00e4;
    return p[0] == 0x02 && p[1] == 0x08 && sleeps_after_08 ? 2 : 0;
}

db_quirk_t db_quirk(uint16_t pid, uint8_t op, const uint8_t *p, size_t len)
{
    db_quirk_t q = {0};
    if (len < 6) {
        return q;
    }
    if (op == DB_CORA_OP_WRITE) {
        q.wait_ms = after_write(pid, p);
    } else if (op == DB_CORA_OP_SEND_REPORT) {
        if (six_key(pid) && p[0] == 0x03 && p[1] == 0x0b && p[2] == 0x00) {
            q.block = true; /* "SD mini not this CMD" */
        } else if (six_key(pid) && p[0] == 0x0b && p[1] == 0x63 && p[2] == 0x02) {
            q.wait_ms = 900; /* reset: the deck restarts */
        } else if (p[0] == 0x03 && (p[1] == 0x04 || p[1] == 0x11 || p[1] == 0x12)) {
            q.wait_ms = 50;
        }
    }
    return q;
}
