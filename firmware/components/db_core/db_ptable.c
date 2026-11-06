#include "db_ptable.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY   32
#define MAX     95
#define TYPE_APP 0
#define TABLE_END 0x9000 /* bootloader at 0x2000, table at 0x8000 */

typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
    char label[17];
} part_t;

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int parse(const uint8_t *t, size_t len, part_t *out)
{
    int n = 0;
    for (size_t i = 0; i + ENTRY <= len && n < MAX && t[i] == 0xAA && t[i + 1] == 0x50; i += ENTRY, n++) {
        out[n] = (part_t){.type = t[i + 2], .subtype = t[i + 3], .offset = u32(t + i + 4), .size = u32(t + i + 8)};
        memcpy(out[n].label, t + i + 12, 16);
        out[n].label[16] = '\0';
    }
    return n;
}

static const part_t *find(const part_t *p, int n, const part_t *like)
{
    for (int i = 0; i < n; i++) {
        if (p[i].type == like->type && p[i].subtype == like->subtype && strcmp(p[i].label, like->label) == 0) {
            return &p[i];
        }
    }
    return NULL;
}

static const char *check(const part_t *a, int na, const part_t *b, int nb, uint32_t running_offset,
                         uint32_t image_len, uint32_t flash_size)
{
    if (nb == 0) {
        return "not a partition table";
    }
    for (int i = 0; i < nb; i++) {
        if ((uint64_t)b[i].offset + b[i].size > flash_size) {
            return "a partition ends past the flash";
        }
        if (b[i].offset < TABLE_END) {
            return "a partition lies on the bootloader or the table";
        }
        if (b[i].type == TYPE_APP && (b[i].offset % 0x10000 || b[i].size < image_len)) {
            return "an app slot is unaligned or smaller than the running firmware";
        }
        for (int k = 0; k < i; k++) {
            if (b[i].offset < b[k].offset + b[k].size && b[k].offset < b[i].offset + b[i].size) {
                return "partitions overlap";
            }
        }
    }
    for (int i = 0; i < na; i++) {
        const part_t *same = find(b, nb, &a[i]);
        if (a[i].type != TYPE_APP && (!same || same->offset != a[i].offset || same->size != a[i].size)) {
            return "a data partition moves, changes size or is gone"; /* settings, otadata, www... */
        }
        if (a[i].offset == running_offset && (!same || same->offset != running_offset)) {
            return "the running slot moves";
        }
    }
    return NULL;
}

const char *db_ptable_check(const uint8_t *now, const uint8_t *next, size_t len, uint32_t running_offset,
                            uint32_t image_len, uint32_t flash_size)
{
    part_t *a = calloc(2 * MAX, sizeof(part_t));
    if (!a) {
        return "out of memory";
    }
    part_t *b = a + MAX;
    const char *err =
        check(a, parse(now, len, a), b, parse(next, len, b), running_offset, image_len, flash_size);
    free(a);
    return err;
}
