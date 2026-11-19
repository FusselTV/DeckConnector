#include "db_evring.h"

#include <string.h>

#define PER (DB_EVRING_ERASE / DB_EVREC_SIZE)

static uint32_t slots(const db_evring_t *r)
{
    return r->f.size / DB_EVREC_SIZE;
}

static uint32_t sectors(const db_evring_t *r)
{
    return r->f.size / DB_EVRING_ERASE;
}

static uint32_t sector_of(const db_evring_t *r, uint32_t seq)
{
    return seq % slots(r) / PER;
}

uint16_t db_evrec_check(const db_evrec_t *rec)
{
    const uint8_t *p = (const uint8_t *)rec;
    uint16_t crc = 0xFFFF; /* CRC-16/CCITT */
    for (size_t i = 0; i < offsetof(db_evrec_t, check); i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (uint16_t)(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

static bool valid(const db_evrec_t *rec, uint32_t slot, uint32_t n_slots)
{
    return rec->seq && rec->seq != UINT32_MAX && rec->seq % n_slots == slot &&
           rec->check == db_evrec_check(rec);
}

static bool blank(const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static int erase(db_evring_t *r, uint32_t sector)
{
    return r->f.erase(sector * DB_EVRING_ERASE, DB_EVRING_ERASE);
}

static int erase_if_used(db_evring_t *r, uint32_t sector, uint8_t *buf)
{
    int err = r->f.read(sector * DB_EVRING_ERASE, buf, DB_EVRING_ERASE);
    return err ? err : blank(buf, DB_EVRING_ERASE) ? 0 : erase(r, sector);
}

static int scan(db_evring_t *r, uint8_t *b)
{
    for (uint32_t s = 0; s < sectors(r); s++) {
        int err = r->f.read(s * DB_EVRING_ERASE, b, DB_EVRING_ERASE);
        if (err) {
            return err;
        }
        for (uint32_t i = 0; i < PER; i++) {
            const db_evrec_t *rec = (const db_evrec_t *)(b + (size_t)i * DB_EVREC_SIZE);
            if (valid(rec, s * PER + i, slots(r))) {
                r->newest = rec->seq > r->newest ? rec->seq : r->newest;
                r->oldest = !r->oldest || rec->seq < r->oldest ? rec->seq : r->oldest;
            }
        }
    }
    return 0;
}

/* What a power cut left behind the newest record is skipped, never written over. */
static int skip_used(db_evring_t *r, uint8_t *b)
{
    uint32_t first = r->next % PER;
    int err = r->f.read(r->sector * DB_EVRING_ERASE, b, DB_EVRING_ERASE);
    for (uint32_t i = PER; !err && i-- > first;) {
        if (blank(b + (size_t)i * DB_EVREC_SIZE, DB_EVREC_SIZE)) {
            continue;
        }
        if (first == 0) {
            return erase(r, r->sector);
        }
        r->next += i - first + 1;
        if (sector_of(r, r->next) != r->sector) {
            r->sector = sector_of(r, r->next);
            return erase_if_used(r, r->sector, b);
        }
        return 0;
    }
    return err;
}

int db_evring_open(db_evring_t *r, const db_evflash_t *f, void *buf)
{
    *r = (db_evring_t){.f = *f};
    if (f->size < 2 * DB_EVRING_ERASE || f->size % DB_EVRING_ERASE) {
        return -1;
    }
    int err = scan(r, buf);
    if (err) {
        return err;
    }
    r->next = r->newest + 1;
    r->sector = sector_of(r, r->next);
    err = skip_used(r, buf);
    uint32_t n = f->size / DB_EVRING_ERASE; /* not sectors(r): the analyzer cannot see r->f stay put */
    return err ? err : erase_if_used(r, (r->sector + 1) % n, buf);
}

int db_evring_append(db_evring_t *r, db_evrec_t *rec)
{
    if (rec->seq < r->next) {
        return -1;
    }
    uint32_t sec = sector_of(r, rec->seq);
    bool lapped = rec->seq - r->next >= slots(r) - PER;
    if (sec != r->sector || lapped) {
        int err = sec != (r->sector + 1) % sectors(r) || lapped ? erase(r, sec) : 0;
        err = err ? err : erase(r, (sec + 1) % sectors(r));
        if (err) {
            return err;
        }
        r->sector = sec;
    }
    rec->check = db_evrec_check(rec);
    int err = r->f.write(rec->seq % slots(r) * DB_EVREC_SIZE, rec, DB_EVREC_SIZE);
    r->next = rec->seq + 1; /* also after a failed write: that slot may hold half a record now */
    if (err) {
        return err;
    }
    uint32_t start = rec->seq - rec->seq % PER;
    uint32_t window = (sectors(r) - 2) * PER;
    uint32_t kept_from = start > window ? start - window : 1;
    r->oldest = !r->oldest ? rec->seq : r->oldest < kept_from ? kept_from : r->oldest;
    r->newest = rec->seq;
    return 0;
}

bool db_evring_get(const db_evring_t *r, uint32_t seq, db_evrec_t *out)
{
    uint32_t slot = seq % slots(r);
    return seq && r->f.read(slot * DB_EVREC_SIZE, out, sizeof(*out)) == 0 && valid(out, slot, slots(r)) &&
           out->seq == seq;
}

void db_evrec_set_args(db_evrec_t *rec, const char *const *args)
{
    memset(rec->args, 0, sizeof(rec->args));
    size_t at = 0;
    for (; args && *args && at < sizeof(rec->args) - 1; args++) {
        size_t n = strlen(*args);
        size_t room = sizeof(rec->args) - 1 - at;
        n = n < room ? n : room;
        memcpy(rec->args + at, *args, n);
        at += n + 1;
    }
}

const char *db_evrec_arg(const db_evrec_t *rec, int n)
{
    size_t at = 0;
    for (; n > 0 && at < sizeof(rec->args); n--) {
        at += strnlen(rec->args + at, sizeof(rec->args) - at) + 1;
    }
    /* the args after the last one are all NUL: an empty string there is the end, not an argument */
    if (at >= sizeof(rec->args) || (!rec->args[at] && (at + 1 >= sizeof(rec->args) || !rec->args[at + 1]))) {
        return NULL;
    }
    return rec->args + at;
}
