#include "db_device.h"

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
