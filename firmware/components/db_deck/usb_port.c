#include "usb_port.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/usb_dwc_struct.h"
#include "usb/usb_host.h"

#define OUT_SLOTS          3 /* output reports in flight; more does not help, the device is the limit */
#define REPORT_LEN         1024 /* every output report, sent in packets of the endpoint size like the Elgato dock */
#define CTRL_BUF           (sizeof(usb_setup_packet_t) + 1024)
#define CTRL_TIMEOUT_MS    1000
#define HID_CLASS          0x03
#define HID_GET_REPORT     0x01
#define HID_SET_REPORT     0x09
#define HID_REPORT_FEATURE 3
/* a fresh deck can take 0.5 s per report; this long with nothing but failed output is a hung deck */
#define STUCK_MS 3000

static const char *TAG = "usb";

static struct {
    usb_port_cb_t cb;
    usb_host_client_handle_t client;
    usb_device_handle_t dev;
    uint8_t intf;
    uint8_t ep_in;
    uint8_t ep_out;
    volatile bool open;
    usb_transfer_t *out[OUT_SLOTS];
    int next_out;
    SemaphoreHandle_t out_free;
    usb_transfer_t *in;
    usb_transfer_t *ctrl;
    SemaphoreHandle_t ctrl_done;
    SemaphoreHandle_t ctrl_lock;
    volatile uint32_t errors;
    uint32_t
        fail_since; /* ms of the first failed output since the last one that went through, 0: none. 32 bits:
                     * written by the USB client task and read by writers, a 64-bit value could tear */
    uint32_t recoveries;
    db_hid_io_t io;
    bool auto_speed;
    volatile bool full; /* the root port runs at full speed */
    bool behind_hub;    /* the open device hangs on a hub, not on the root port */
    esp_timer_handle_t check;
    esp_timer_handle_t in_retry;
    uint32_t in_backoff_ms;
    uint32_t replug_until_ms; /* our own replug empties the port for a moment; 32 bits, see fail_since */
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
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        __atomic_store_n(&s.fail_since, 0, __ATOMIC_RELAXED);
    }
    count_error(t->status);
    xSemaphoreGive(s.out_free);
}

static void in_resubmit(usb_transfer_t *t);

static void in_done(usb_transfer_t *t)
{
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0) {
        s.cb.input(t->data_buffer, t->actual_num_bytes, (uint64_t)esp_timer_get_time());
    }
    count_error(t->status);
    /* CANCELED while open (a flush) is queued again below */
    if (!s.open || t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        return;
    }
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        /* an error that repeats would otherwise spin the client task: wait longer each time, up to 1 s */
        if (s.in_backoff_ms == 1000) {
            ESP_LOGE(TAG, "input keeps failing (status %d): reconnecting the deck", t->status);
            s.cb.stuck();
        }
        s.in_backoff_ms = s.in_backoff_ms ? (s.in_backoff_ms * 2 > 1000 ? 1000 : s.in_backoff_ms * 2) : 100;
        if (s.in_backoff_ms < 1000) {
            ESP_LOGW(TAG, "input transfer ended with status %d", t->status);
        }
        esp_timer_start_once(s.in_retry, s.in_backoff_ms * 1000);
        return;
    }
    s.in_backoff_ms = 0;
    in_resubmit(t);
}

static void in_resubmit(usb_transfer_t *t)
{
    /* Without a pending IN transfer no key or encoder would ever arrive again. An error halts the endpoint.
     */
    if (usb_host_transfer_submit(t) != ESP_OK &&
        (usb_host_endpoint_clear(s.dev, s.ep_in) != ESP_OK || usb_host_transfer_submit(t) != ESP_OK)) {
        s.errors++;
        ESP_LOGE(TAG, "input transfer could not be queued: reconnecting the deck");
        s.cb.stuck();
        esp_timer_start_once(s.in_retry, 1000000); /* stuck() may hold back a replug for a while */
    }
}

static void ctrl_done(usb_transfer_t *t)
{
    (void)t;
    xSemaphoreGive(s.ctrl_done);
}

static db_err_t control(uint8_t type, uint8_t request, uint8_t report_id, uint8_t *data, size_t len,
                        size_t *out_len);

/* On 2026-11-08 the deck's output transfers stopped completing while it stayed attached: it showed one
 * picture for hours and the errors were only counted. Cutting the port's power brought it back. */
static db_err_t write_failed(db_err_t err)
{
    s.errors++;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000) | 1;
    uint32_t since = __atomic_load_n(&s.fail_since, __ATOMIC_RELAXED);
    if (!since) {
        __atomic_store_n(&s.fail_since, now, __ATOMIC_RELAXED);
    } else if (now - since >= STUCK_MS) {
        __atomic_store_n(&s.fail_since, 0, __ATOMIC_RELAXED);
        s.recoveries++;
        /* Channel enabled and HCDMA unchanged over 100 ms: the host channel is stuck (esp-usb #627).
         * Channel done or gone: the deck refuses the data. */
        for (int pass = 0; pass < 2; pass++) {
            for (int c = 0; c < 16; c++) {
                volatile usb_dwc_host_chan_regs_t *ch = &USB_DWC_HS.host_chans[c];
                if (ch->hcchar_reg.chena) {
                    ESP_LOGE(TAG, "chan %d hcchar %08lx hctsiz %08lx hcdma %08lx hcint %08lx hfnum %08lx", c,
                             (unsigned long)ch->hcchar_reg.val, (unsigned long)ch->hctsiz_reg.val,
                             (unsigned long)ch->hcdma_reg.val, (unsigned long)ch->hcint_reg.val,
                             (unsigned long)USB_DWC_HS.hfnum_reg.val);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        uint8_t probe[32] = {0x05};
        bool answers = control(0xa1, HID_GET_REPORT, probe[0], probe, sizeof(probe), NULL) == DB_OK;
        ESP_LOGE(TAG, "the deck took no output for %d s, its control endpoint %s: reconnecting it",
                 STUCK_MS / 1000, answers ? "still answers" : "is silent too");
        s.cb.stuck();
    }
    return err;
}

static db_err_t io_write(const uint8_t *report, size_t len)
{
    if (!s.open || len > REPORT_LEN) {
        return DB_ERR_STATE;
    }
    /* A device that hangs without unplugging must not block the writer forever. */
    if (xSemaphoreTake(s.out_free, pdMS_TO_TICKS(500)) != pdTRUE) {
        return write_failed(DB_ERR_TIMEOUT);
    }
    usb_transfer_t *t = s.out[s.next_out];
    s.next_out = (s.next_out + 1) % OUT_SLOTS;
    memcpy(t->data_buffer, report, len);
    t->num_bytes = len;
    if (usb_host_transfer_submit(t) != ESP_OK) {
        xSemaphoreGive(s.out_free);
        return write_failed(DB_ERR_IO);
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
        if (s.full && s.auto_speed) { /* the hub may have gone with it: check_speed goes back to high speed */
            esp_timer_stop(s.check);
            esp_timer_start_once(s.check, 1500000);
        }
    }
}

/* Full speed only: some hubs drop a high speed deck right after the handshake, and full speed behind a
 * high speed hub would need a transaction translator the host stack lacks. IDF 6.0's HAL cannot do esp-usb's
 * fsls_only, so the bit is set by hand; a recovery soft reset clears it, hence after every round of events.
 */
static void keep_full_speed(void)
{
    if (s.full) {
        USB_DWC_HS.hcfg_reg.fslssupp = 1;
    }
}

static void lib_task(void *arg)
{
    /* The deck has a 1024 byte interrupt OUT endpoint. The default FIFO split caps periodic OUT at
     * 512 bytes, and the PERIODIC_OUT bias leaves RX one line short for its 512 byte IN endpoint
     * (esp-idf #19143). Total must stay at or below 896 lines. */
    usb_host_config_t cfg = {
        .intr_flags = ESP_INTR_FLAG_LOWMED,
        .fifo_settings_custom = {.rx_fifo_lines = 260, .ptx_fifo_lines = 572, .nptx_fifo_lines = 64},
    };
    ESP_ERROR_CHECK(usb_host_install(&cfg));
    keep_full_speed();
    xTaskNotifyGive((TaskHandle_t)arg);
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        keep_full_speed();
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

static esp_err_t replug(uint8_t address, uint32_t off_ms);

/* one at a time: a second would switch the port on in the middle of the first one's off time */
esp_err_t usb_port_replug(uint8_t address, uint32_t off_ms)
{
    static bool busy;
    if (__atomic_exchange_n(&busy, true, __ATOMIC_RELAXED)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = replug(address, off_ms);
    __atomic_store_n(&busy, false, __ATOMIC_RELAXED);
    return err;
}

static esp_err_t replug(uint8_t address, uint32_t off_ms)
{
    usb_device_handle_t dev;
    if (address && usb_host_device_open(s.client, address, &dev) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (address) {
        usb_host_device_close(s.client, dev);
    }
    /* A port without power is a pulled cable to the host stack: the device goes, and comes back enumerated
     * anew. Always the root port, so behind a hub everything on it goes too: switching a hub port from here
     * (CLEAR_FEATURE on the hub) tripped an assert in esp-usb's ext_hub.c:512 and restarted the bridge. */
    ESP_LOGI(TAG, "power cycling the root port");
    __atomic_store_n(&s.replug_until_ms, (uint32_t)(esp_timer_get_time() / 1000) + off_ms + 4500,
                     __ATOMIC_RELAXED);
    ESP_RETURN_ON_ERROR(usb_host_lib_set_root_port_power(false), TAG, "port off");
    vTaskDelay(pdMS_TO_TICKS(off_ms));
    keep_full_speed();
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK;
         i++) { /* off and never on again: the deck stays dark until a reboot */
        err = usb_host_lib_set_root_port_power(true);
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

static void replug_task(void *arg)
{
    usb_port_replug(0, 500); /* it sleeps: not in the esp_timer task */
    vTaskDelete(NULL);
}

static void in_retry(void *arg)
{
    if (s.open && s.in) {
        in_resubmit(s.in);
    }
}

/* auto: a USB device is there (a hub counts) but no deck came up at high speed, so try full speed. Back to
 * high speed once the port is empty, so a deck plugged in directly afterwards gets it. */
static void check_speed(void *arg)
{
    uint8_t addrs[8];
    int n = 0;
    if (s.dev || usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) != ESP_OK) {
        return;
    }
    if (s.full) {
        int32_t left = (int32_t)(__atomic_load_n(&s.replug_until_ms, __ATOMIC_RELAXED) -
                                 (uint32_t)(esp_timer_get_time() / 1000));
        if (n == 0 && s.auto_speed && left < 0) {
            ESP_LOGI(TAG, "nothing on the USB port any more: back to high speed");
            s.full = false;
            USB_DWC_HS.hcfg_reg.fslssupp = 0;
        }
        return;
    }
    if (n == 0) {
        return;
    }
    ESP_LOGW(TAG, "no deck at high speed with %d USB device(s) present: trying full speed", n);
    s.full = true;
    if (xTaskCreate(replug_task, "usb_replug", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no task to switch to full speed");
    }
}

bool usb_port_full_speed(void)
{
    return s.full;
}

bool usb_port_behind_hub(void)
{
    return s.dev && s.behind_hub;
}

esp_err_t usb_port_start(const usb_port_cb_t *cb, bool full_only)
{
    s.cb = *cb;
    s.auto_speed = !full_only;
    s.full = full_only;
    esp_timer_create_args_t ta = {.callback = check_speed, .name = "usb_speed"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s.check), TAG, "timer");
    esp_timer_create_args_t ra = {.callback = in_retry, .name = "usb_in_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ra, &s.in_retry), TAG, "timer");
    s.out_free = xSemaphoreCreateCounting(OUT_SLOTS, OUT_SLOTS);
    s.ctrl_done = xSemaphoreCreateBinary();
    s.ctrl_lock = xSemaphoreCreateMutex();
    s.io = (db_hid_io_t){
        .write = io_write, .flush = io_flush, .get_feature = io_get_feature, .set_feature = io_set_feature};
    if (!s.out_free || !s.ctrl_done || !s.ctrl_lock ||
        xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 1) !=
            pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    usb_host_client_config_t cc = {
        .max_num_event_msg = 5,
        .async = {.client_event_callback = client_event},
    };
    ESP_RETURN_ON_ERROR(usb_host_client_register(&cc, &s.client), TAG, "client register");
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(CTRL_BUF, 0, &s.ctrl), TAG, "ctrl alloc");
    s.ctrl->callback = ctrl_done;
    if (xTaskCreatePinnedToCore(client_task, "usb_client", 4096, NULL, 9, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (s.auto_speed) {
        esp_timer_start_once(s.check, 6000000);
    }
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
                /* IN sizes must be a multiple of the packet size; 512 also holds the Mini's 65 byte report */
                int in_len = USB_EP_DESC_GET_MPS(ep) > 512 ? USB_EP_DESC_GET_MPS(ep) : 512;
                if (!s.in && usb_host_transfer_alloc(in_len, 0, &s.in) != ESP_OK) {
                    return ESP_ERR_NO_MEM;
                }
                s.ep_in = ep->bEndpointAddress;
                s.in->num_bytes = in_len;
            } else {
                s.ep_out = ep->bEndpointAddress;
            }
        }
        if (s.ep_in && s.ep_out) {
            s.intf = intf->bInterfaceNumber;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/* At full speed the deck declares 512 byte interrupt endpoints. IN works at 512 (at 64 every poll
 * overflows), but the deck stalls OUT packets above 64, so output reports go out in 64 byte packets. */
static void clamp_fs_out_mps(const usb_config_desc_t *cd)
{
    int off = 0;
    const usb_standard_desc_t *d = (const usb_standard_desc_t *)cd;
    while (
        (d = usb_parse_next_descriptor_of_type(d, cd->wTotalLength, USB_B_DESCRIPTOR_TYPE_ENDPOINT, &off))) {
        usb_ep_desc_t *ep = (usb_ep_desc_t *)d;
        if (!(ep->bEndpointAddress & 0x80) && USB_EP_DESC_GET_XFERTYPE(ep) == USB_TRANSFER_TYPE_INTR &&
            USB_EP_DESC_GET_MPS(ep) > 64) {
            ep->wMaxPacketSize = 64;
        }
    }
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

esp_err_t usb_port_ids(uint8_t address, uint16_t *vid, uint16_t *pid)
{
    usb_device_handle_t d;
    ESP_RETURN_ON_ERROR(usb_host_device_open(s.client, address, &d), TAG, "open");
    const usb_device_desc_t *dd;
    usb_host_get_device_descriptor(d, &dd);
    *vid = dd->idVendor;
    *pid = dd->idProduct;
    return usb_host_device_close(s.client, d);
}

void usb_port_product(char *out, size_t cap)
{
    usb_device_info_t info;
    const usb_str_desc_t *d = NULL;
    if (s.dev && usb_host_device_info(s.dev, &info) == ESP_OK) {
        d = info.str_desc_product;
    }
    size_t n = 0;
    for (int i = 0; d && i < (d->bLength - 2) / 2 && n + 1 < cap; i++) {
        out[n++] = d->wData[i] < 0x80 ? (char)d->wData[i] : '?';
    }
    out[n] = '\0';
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
    s.behind_hub = info.parent.dev_hdl != NULL;
    *vid = dd->idVendor;
    *pid = dd->idProduct;
    ESP_LOGI(TAG, "device %04x:%04x, %s speed", *vid, *pid, info.speed == USB_SPEED_HIGH ? "high" : "full");
    esp_err_t err = find_endpoints(cd);
    if (info.speed != USB_SPEED_HIGH) {
        clamp_fs_out_mps(cd);
    }
    if (err == ESP_OK) {
        err = usb_host_interface_claim(s.client, s.dev, s.intf, 0);
    }
    for (int i = 0; err == ESP_OK && i < OUT_SLOTS; i++) {
        err = usb_host_transfer_alloc(REPORT_LEN, 0, &s.out[i]);
        if (err == ESP_OK) {
            s.out[i]->device_handle = s.dev;
            s.out[i]->bEndpointAddress = s.ep_out;
            s.out[i]->callback = out_done;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no usable HID interface: %s", esp_err_to_name(err));
        if (s.auto_speed) { /* a dock's own device came up; the deck behind it may not */
            esp_timer_stop(s.check);
            esp_timer_start_once(s.check, 3000000);
        }
        free_transfers();
        usb_host_device_close(s.client, s.dev);
        s.dev = NULL;
        return err;
    }
    s.ctrl->device_handle = s.dev;
    s.in->device_handle = s.dev;
    s.in->bEndpointAddress = s.ep_in;
    s.in->callback = in_done;
    s.io.out_report_size = REPORT_LEN;
    s.next_out = 0;
    s.fail_since = 0;
    s.open = true;
    s.in_backoff_ms = 0;
    in_resubmit(s.in);
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

uint32_t usb_port_recoveries(void)
{
    return s.recoveries;
}

esp_err_t usb_port_stall(void)
{
    return s.open ? usb_host_endpoint_halt(s.dev, s.ep_out) : ESP_ERR_INVALID_STATE;
}
