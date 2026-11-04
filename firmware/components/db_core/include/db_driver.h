#pragma once

#include "db_device.h"

/*
 * A driver speaks the USB HID protocol of one device family. It never touches USB directly: the port layer
 * hands it a db_hid_io_t. Drivers are stateless apart from the per-device state they keep in
 * `db_dev_t.state`.
 */

typedef struct {
    /* Queue one output report. May return before the transfer finished. */
    db_err_t (*write)(const uint8_t *report, size_t len);
    /* Wait until every queued output report finished. */
    db_err_t (*flush)(void);
    /* `buf[0]` must hold the report id on entry. */
    db_err_t (*get_feature)(uint8_t *buf, size_t len, size_t *out_len);
    db_err_t (*set_feature)(const uint8_t *buf, size_t len);
    size_t out_report_size;
} db_hid_io_t;

typedef struct db_driver db_driver_t;

typedef struct {
    const db_driver_t *driver;
    const db_hid_io_t *io;
    db_device_info_t info;
    _Alignas(8) uint8_t state[64]; /* driver private, e.g. last key states for edge detection */
} db_dev_t;

struct db_driver {
    const char *name;
    bool (*match)(uint16_t vid, uint16_t pid);
    /* Fill `dev->info`. `dev->io` and `dev->driver` are set by the caller. */
    db_err_t (*open)(db_dev_t *dev, uint16_t vid, uint16_t pid);
    /* Send one image for `target`. `rect` is inside the target and in display coordinates. */
    db_err_t (*write_image)(db_dev_t *dev, const db_target_t *target, db_rect_t rect, const uint8_t *data,
                            size_t len);
    /* Turn one input report into events. Returns how many were written to `out`. */
    size_t (*parse_input)(db_dev_t *dev, const uint8_t *report, size_t len, uint64_t t_us, db_input_t *out,
                          size_t max);
    db_err_t (*set_brightness)(db_dev_t *dev, uint8_t pct);
};

const db_driver_t *db_driver_find(uint16_t vid, uint16_t pid);
