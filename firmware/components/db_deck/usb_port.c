#include "usb_port.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define OUT_SLOTS          3 /* output reports in flight; more does not help, the device is the limit */
#define CTRL_BUF           (sizeof(usb_setup_packet_t) + 1024)
#define CTRL_TIMEOUT_MS    1000
#define HID_CLASS          0x03
#define HID_GET_REPORT     0x01
#define HID_SET_REPORT     0x09
#define HID_REPORT_FEATURE 3

static const char *TAG = "usb";

static struct {
    usb_port_cb_t cb;
    usb_host_client_handle_t client;
    usb_device_handle_t dev;
    uint8_t intf;
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t mps_out;
    volatile bool open;
    usb_transfer_t *out[OUT_SLOTS];
    int next_out;
    SemaphoreHandle_t out_free;
    usb_transfer_t *in;
    usb_transfer_t *ctrl;
    SemaphoreHandle_t ctrl_done;
    SemaphoreHandle_t ctrl_lock;
    volatile uint32_t errors;
    db_hid_io_t io;
} s;

/* An unplug ends every transfer in flight; only other endings are errors. */
static void count_error(usb_transfer_status_t status)
{
    if (status != USB_TRANSFER_STATUS_COMPLETED && status != USB_TRANSFER_STATUS_NO_DEVICE &&
        status != USB_TRANSFER_STATUS_CANCELED) {
        s.errors++;
    }
}

static void out_done(usb_transfer_t *t)
{
    count_error(t->status);
    xSemaphoreGive(s.out_free);
}

static void in_done(usb_transfer_t *t)
{
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0) {
        s.cb.input(t->data_buffer, t->actual_num_bytes, (uint64_t)esp_timer_get_time());
    }
    count_error(t->status);
    /* Without a pending IN transfer no key or encoder would ever arrive again. */
    if (s.open && t->status != USB_TRANSFER_STATUS_NO_DEVICE && t->status != USB_TRANSFER_STATUS_CANCELED &&
        usb_host_transfer_submit(t) != ESP_OK) {
        s.errors++;
        ESP_LOGE(TAG, "input transfer could not be queued again, input stops until the device is replugged");
    }
}

static void ctrl_done(usb_transfer_t *t)
{
    (void)t;
    xSemaphoreGive(s.ctrl_done);
}

static db_err_t io_write(const uint8_t *report, size_t len)
{
    if (!s.open || len > s.mps_out) {
        return DB_ERR_STATE;
    }
    /* A device that hangs without unplugging must not block the writer forever. */
    if (xSemaphoreTake(s.out_free, pdMS_TO_TICKS(500)) != pdTRUE) {
        s.errors++;
        return DB_ERR_TIMEOUT;
    }
    usb_transfer_t *t = s.out[s.next_out];
    s.next_out = (s.next_out + 1) % OUT_SLOTS;
    memcpy(t->data_buffer, report, len);
    t->num_bytes = len;
    if (usb_host_transfer_submit(t) != ESP_OK) {
        s.errors++;
        xSemaphoreGive(s.out_free);
        return DB_ERR_IO;
    }
    return DB_OK;
}

static db_err_t io_flush(void)
{
    for (int i = 0; i < OUT_SLOTS; i++) {
        if (xSemaphoreTake(s.out_free, pdMS_TO_TICKS(500)) != pdTRUE) {
            for (int j = 0; j < i; j++) {
                xSemaphoreGive(s.out_free);
            }
            return DB_ERR_TIMEOUT;
        }
    }
    for (int i = 0; i < OUT_SLOTS; i++) {
        xSemaphoreGive(s.out_free);
    }
    return DB_OK;
}

static db_err_t control(uint8_t type, uint8_t request, uint8_t report_id, uint8_t *data, size_t len,
                        size_t *out_len)
{
    if (!s.open || len > CTRL_BUF - sizeof(usb_setup_packet_t)) {
        return DB_ERR_STATE;
    }
    xSemaphoreTake(s.ctrl_lock, portMAX_DELAY);
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s.ctrl->data_buffer;
    *setup = (usb_setup_packet_t){.bmRequestType = type,
                                  .bRequest = request,
                                  .wValue = (HID_REPORT_FEATURE << 8) | report_id,
                                  .wIndex = s.intf,
                                  .wLength = (uint16_t)len};
    bool in = type & 0x80;
    if (!in) {
        memcpy(s.ctrl->data_buffer + sizeof(*setup), data, len);
    }
    s.ctrl->num_bytes = (int)(sizeof(*setup) + len);
    db_err_t res = DB_ERR_IO;
    xSemaphoreTake(s.ctrl_done, 0); /* drop a late completion of an earlier, timed out transfer */
    bool submitted = usb_host_transfer_submit_control(s.client, s.ctrl) == ESP_OK;
    bool done = submitted && xSemaphoreTake(s.ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS)) == pdTRUE;
    if (submitted && !done) {
        s.errors++;
        /* The transfer still owns the buffer; wait for it before anyone reuses s.ctrl. */
        xSemaphoreTake(s.ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS));
    }
    if (done && s.ctrl->status == USB_TRANSFER_STATUS_COMPLETED) {
        size_t got = (size_t)s.ctrl->actual_num_bytes - sizeof(*setup);
        if (in) {
            memcpy(data, s.ctrl->data_buffer + sizeof(*setup), got);
        }
        if (out_len) {
            *out_len = got;
        }
        res = DB_OK;
    }
    xSemaphoreGive(s.ctrl_lock);
    return res;
}

static db_err_t io_get_feature(uint8_t *buf, size_t len, size_t *out_len)
{
    return control(0xa1, HID_GET_REPORT, buf[0], buf, len, out_len);
}

static db_err_t io_set_feature(const uint8_t *buf, size_t len)
{
    return control(0x21, HID_SET_REPORT, buf[0], (uint8_t *)buf, len, NULL);
}

static void client_event(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s.cb.attached(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl == s.dev) {
        s.open = false;
        s.cb.detached();
    }
}

static void lib_task(void *arg)
{
    /* The Stream Deck has a 1024 byte interrupt OUT endpoint. The default FIFO split caps periodic OUT at
     * 512 bytes, and the PERIODIC_OUT bias leaves RX one line short for its 512 byte IN endpoint
     * (esp-idf #19143). Total must stay at or below 896 lines. */
    usb_host_config_t cfg = {
        .intr_flags = ESP_INTR_FLAG_LOWMED,
        .fifo_settings_custom = {.rx_fifo_lines = 260, .ptx_fifo_lines = 572, .nptx_fifo_lines = 64},
    };
    ESP_ERROR_CHECK(usb_host_install(&cfg));
    xTaskNotifyGive((TaskHandle_t)arg);
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void client_task(void *arg)
{
    for (;;) {
        usb_host_client_handle_events(s.client, portMAX_DELAY);
    }
}

/* SET_FEATURE or CLEAR_FEATURE(PORT_POWER) on a port of the hub the device hangs on (USB 2.0 11.24.2). */
static esp_err_t hub_port_power(const usb_parent_dev_info_t *parent, bool on)
{
    usb_device_info_t hub_info;
    usb_device_handle_t hub;
    ESP_RETURN_ON_ERROR(usb_host_device_info(parent->dev_hdl, &hub_info), TAG, "hub info");
    ESP_RETURN_ON_ERROR(usb_host_device_open(s.client, hub_info.dev_addr, &hub), TAG, "hub open");
    usb_transfer_t *t;
    esp_err_t err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &t);
    if (err == ESP_OK) {
        *(usb_setup_packet_t *)t->data_buffer = (usb_setup_packet_t){
            .bmRequestType = 0x23, .bRequest = on ? 3 : 1, .wValue = 8, .wIndex = parent->port_num};
        t->num_bytes = sizeof(usb_setup_packet_t);
        t->device_handle = hub;
        t->callback = ctrl_done;
        xSemaphoreTake(s.ctrl_lock, portMAX_DELAY); /* ctrl_done is shared with the deck's control transfers */
        xSemaphoreTake(s.ctrl_done, 0);
        err = usb_host_transfer_submit_control(s.client, t);
        if (err == ESP_OK && xSemaphoreTake(s.ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS)) != pdTRUE) {
            err = ESP_ERR_TIMEOUT; /* t is still in flight: leak it rather than free it under the stack */
            t = NULL;
        } else if (err == ESP_OK && t->status != USB_TRANSFER_STATUS_COMPLETED) {
            err = ESP_FAIL;
        }
        xSemaphoreGive(s.ctrl_lock);
        if (t) {
            usb_host_transfer_free(t);
        }
    }
    usb_host_device_close(s.client, hub);
    return err;
}

static esp_err_t port_power(const usb_parent_dev_info_t *hub, bool on)
{
    return hub->dev_hdl ? hub_port_power(hub, on) : usb_host_lib_set_root_port_power(on);
}

esp_err_t usb_port_replug(uint8_t address)
{
    usb_device_info_t info = {0};
    usb_device_handle_t dev = address ? NULL : s.dev; /* with an address only that device, never ours instead */
    bool opened = address && usb_host_device_open(s.client, address, &dev) == ESP_OK;
    if (address && !opened) {
        return ESP_ERR_NOT_FOUND;
    }
    if (dev) { /* none: the root port, which also wakes a deck that never came up */
        usb_host_device_info(dev, &info);
    }
    if (opened) {
        usb_host_device_close(s.client, dev);
    }
    /* a port without power is a pulled cable to the host stack: the device goes, and comes back enumerated anew.
     * Behind a hub only the deck's own port, so other decks on that hub stay. */
    ESP_LOGI(TAG, "power cycling %s port %u", info.parent.dev_hdl ? "hub" : "root", info.parent.port_num);
    ESP_RETURN_ON_ERROR(port_power(&info.parent, false), TAG, "port off");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) { /* off and never on again: the deck stays dark until a reboot */
        err = port_power(&info.parent, true);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (err != ESP_OK) {
        s.errors++;
        ESP_LOGE(TAG, "the deck's port stays without power: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t usb_port_start(const usb_port_cb_t *cb)
{
    s.cb = *cb;
    s.out_free = xSemaphoreCreateCounting(OUT_SLOTS, OUT_SLOTS);
    s.ctrl_done = xSemaphoreCreateBinary();
    s.ctrl_lock = xSemaphoreCreateMutex();
    s.io = (db_hid_io_t){
        .write = io_write, .flush = io_flush, .get_feature = io_get_feature, .set_feature = io_set_feature};
    xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 1);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    usb_host_client_config_t cc = {
        .max_num_event_msg = 5,
        .async = {.client_event_callback = client_event},
    };
    ESP_RETURN_ON_ERROR(usb_host_client_register(&cc, &s.client), TAG, "client register");
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(CTRL_BUF, 0, &s.ctrl), TAG, "ctrl alloc");
    s.ctrl->callback = ctrl_done;
    xTaskCreatePinnedToCore(client_task, "usb_client", 4096, NULL, 9, NULL, 1);
    return ESP_OK;
}

/* First HID interface with an interrupt IN and OUT endpoint. */
static esp_err_t find_endpoints(const usb_config_desc_t *cd)
{
    int off = 0;
    for (int i = 0; i < cd->bNumInterfaces; i++) {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cd, (uint8_t)i, 0, &off);
        if (!intf || intf->bInterfaceClass != HID_CLASS) {
            continue;
        }
        s.ep_in = s.ep_out = 0;
        for (int e = 0; e < intf->bNumEndpoints; e++) {
            int eo = off;
            const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(intf, e, cd->wTotalLength, &eo);
            if (!ep) {
                continue;
            }
            if (ep->bEndpointAddress & 0x80) {
                if (!s.in && usb_host_transfer_alloc(USB_EP_DESC_GET_MPS(ep), 0, &s.in) != ESP_OK) {
                    return ESP_ERR_NO_MEM;
                }
                s.ep_in = ep->bEndpointAddress;
                s.in->num_bytes = USB_EP_DESC_GET_MPS(ep);
            } else {
                s.ep_out = ep->bEndpointAddress;
                s.mps_out = USB_EP_DESC_GET_MPS(ep);
            }
        }
        if (s.ep_in && s.ep_out) {
            s.intf = intf->bInterfaceNumber;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void free_transfers(void)
{
    for (int i = 0; i < OUT_SLOTS; i++) {
        if (s.out[i]) {
            usb_host_transfer_free(s.out[i]);
            s.out[i] = NULL;
        }
    }
    if (s.in) {
        usb_host_transfer_free(s.in);
        s.in = NULL;
    }
}

esp_err_t usb_port_open(uint8_t address, uint16_t *vid, uint16_t *pid)
{
    if (s.dev) {
        ESP_LOGW(TAG, "device %u ignored: one deck at a time", address); /* ponytail: until several decks */
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(usb_host_device_open(s.client, address, &s.dev), TAG, "open");
    const usb_device_desc_t *dd;
    const usb_config_desc_t *cd;
    usb_device_info_t info;
    usb_host_get_device_descriptor(s.dev, &dd);
    usb_host_get_active_config_descriptor(s.dev, &cd);
    usb_host_device_info(s.dev, &info);
    *vid = dd->idVendor;
    *pid = dd->idProduct;
    ESP_LOGI(TAG, "device %04x:%04x, %s speed", *vid, *pid, info.speed == USB_SPEED_HIGH ? "high" : "full");
    esp_err_t err = find_endpoints(cd);
    if (err == ESP_OK) {
        err = usb_host_interface_claim(s.client, s.dev, s.intf, 0);
    }
    for (int i = 0; err == ESP_OK && i < OUT_SLOTS; i++) {
        err = usb_host_transfer_alloc(s.mps_out, 0, &s.out[i]);
        if (err == ESP_OK) {
            s.out[i]->device_handle = s.dev;
            s.out[i]->bEndpointAddress = s.ep_out;
            s.out[i]->callback = out_done;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no usable HID interface: %s", esp_err_to_name(err));
        free_transfers();
        usb_host_device_close(s.client, s.dev);
        s.dev = NULL;
        return err;
    }
    s.ctrl->device_handle = s.dev;
    s.in->device_handle = s.dev;
    s.in->bEndpointAddress = s.ep_in;
    s.in->callback = in_done;
    s.io.out_report_size = s.mps_out;
    s.next_out = 0;
    s.open = true;
    if (usb_host_transfer_submit(s.in) != ESP_OK) {
        ESP_LOGE(TAG, "input transfer could not be queued, the device will show images but send no input");
    }
    return ESP_OK;
}

void usb_port_close(void)
{
    s.open = false;
    if (!s.dev) {
        return;
    }
    /* Let every transfer come back before the endpoints go away. */
    io_flush();
    usb_host_endpoint_halt(s.dev, s.ep_in);
    usb_host_endpoint_flush(s.dev, s.ep_in);
    vTaskDelay(pdMS_TO_TICKS(20));
    usb_host_interface_release(s.client, s.dev, s.intf);
    free_transfers();
    usb_host_device_close(s.client, s.dev);
    s.dev = NULL;
}

const db_hid_io_t *usb_port_io(void)
{
    return &s.io;
}

uint32_t usb_port_errors(void)
{
    return s.errors;
}
