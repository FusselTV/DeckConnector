#pragma once

#include <stdint.h>

#include "db_driver.h"
#include "esp_err.h"

/* USB host side of one HID device. Callbacks run in the USB client task and must not block. */
typedef struct {
    void (*attached)(uint8_t address);
    void (*detached)(void);
    void (*input)(const uint8_t *report, size_t len, uint64_t t_us);
} usb_port_cb_t;

esp_err_t usb_port_start(const usb_port_cb_t *cb);

/* Open and claim the device announced by `attached`. Call from a task other than the USB client task. */
esp_err_t usb_port_open(uint8_t address, uint16_t *vid, uint16_t *pid);
void usb_port_close(void);

/* Valid after usb_port_open() returned ESP_OK. */
const db_hid_io_t *usb_port_io(void);

uint32_t usb_port_errors(void);
/* Cut and restore the port, as if the cable were pulled and plugged in again. Blocks for half a second. */
esp_err_t usb_port_replug(void);
