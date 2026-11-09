#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_driver.h"
#include "esp_err.h"

/* USB host side of one HID device. Callbacks run in the USB client task and must not block. */
typedef struct {
    void (*attached)(uint8_t address);
    void (*detached)(void);
    void (*input)(const uint8_t *report, size_t len, uint64_t t_us);
    void (*stuck)(void); /* output failed for STUCK_MS; runs in the task that wrote (the deck writer or a dock
                          * host), which holds the USB lock */
} usb_port_cb_t;

/* full_only: full speed always; else high speed, and full speed once when no deck comes up */
esp_err_t usb_port_start(const usb_port_cb_t *cb, bool full_only);
bool usb_port_full_speed(void);
bool usb_port_behind_hub(void);

/* Open and claim the device announced by `attached`. Call from a task other than the USB client task. */
esp_err_t usb_port_open(uint8_t address, uint16_t *vid, uint16_t *pid);
/* Vendor and product of a present device, without claiming it. */
esp_err_t usb_port_ids(uint8_t address, uint16_t *vid, uint16_t *pid);
void usb_port_close(void);
/* The open device's USB product string as ASCII, empty without one. */
void usb_port_product(char *out, size_t cap);

/* Valid after usb_port_open() returned ESP_OK. */
const db_hid_io_t *usb_port_io(void);

uint32_t usb_port_errors(void);
uint32_t usb_port_recoveries(void);
/* Halt the output endpoint, as a hung deck would look; for the test of the recovery. */
esp_err_t usb_port_stall(void);
/* Cut and restore the power of the root port, as if the cable were pulled and plugged in again; behind a hub
 * everything on it goes too. `address` (0: none) must be a present device. Blocks for `off_ms`. */
esp_err_t usb_port_replug(uint8_t address, uint32_t off_ms);
