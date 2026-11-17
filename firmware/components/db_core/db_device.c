#include "db_device.h"

#include <stdio.h>
#include <string.h>

const db_target_t *db_device_find_target(const db_device_info_t *info, uint8_t display, db_rect_t rect)
{
    if (display >= info->display_count) {
        return NULL;
    }
    const db_display_t *d = &info->displays[display];
    for (uint8_t i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        if (db_rect_eq(t->rect, rect) || (t->partial && db_rect_contains(t->rect, rect))) {
            return t;
        }
    }
    return NULL;
}

uint8_t db_device_image_keys(const db_device_info_t *info)
{
    uint8_t n = 0;
    for (uint8_t d = 0; d < info->display_count; d++) {
        for (uint8_t i = 0; i < info->displays[d].target_count; i++) {
            n = (uint8_t)(n + (info->displays[d].targets[i].kind == DB_TARGET_KEY));
        }
    }
    return n;
}

bool db_slot_others(const db_slot_owner_t *owners, const bool *taken, int limit, const char *id)
{
    bool others = false;
    for (int i = 0; i < limit; i++) {
        if (!taken[i]) {
            if (!owners[i][0] || strcmp(owners[i], id) == 0) {
                return false;
            }
            others = true;
        }
    }
    return others;
}

int db_slot_take(db_slot_owner_t *owners, const bool *taken, int n, int limit, const char *id)
{
    int own = -1;
    int unowned = -1;
    int any = -1;
    bool alone = true;
    for (int i = n - 1; i >= 0; i--) {
        alone &= !taken[i];
        if (taken[i] || i >= limit) {
            continue;
        }
        own = strcmp(owners[i], id) == 0 ? i : own;
        unowned = owners[i][0] ? unowned : i;
        any = i;
    }
    int slot = own >= 0 ? own : alone || unowned < 0 ? any : unowned;
    if (slot < 0) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(owners[i], id) == 0) {
            owners[i][0] = '\0';
        }
    }
    snprintf(owners[slot], sizeof(owners[slot]), "%s", id);
    return slot;
}
