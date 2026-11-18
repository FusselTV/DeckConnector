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

#define OUT_SLOTS  3    /* output reports in flight; more does not help, the device is the limit */
#define REPORT_LEN 1024 /* every output report, sent in packets of the endpoint size like the Elgato dock */
#define CTRL_BUF   (sizeof(usb_setup_packet_t) + 1024)
#define CTRL_TIMEOUT_MS    1000
#define HID_CLASS          0x03
#define HID_GET_REPORT     0x01
#define HID_SET_REPORT     0x09
#define HID_REPORT_FEATURE 3
#define KEPT_REPORTS       4
/* a fresh deck can take 0.5 s per report; this long with nothing but failed output is a hung deck */
#define STUCK_MS 3000

static const char *TAG = "usb";

typedef struct {
    db_hid_io_t io; /* first: the io callbacks find their device by it */
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
    uint32_t
        fail_since; /* ms of the first failed output since the last one that went through, 0: none. 32 bits:
                     * written by the USB client task and read by writers, a 64-bit value could tear */
    char path[16];  /* hub ports from the root, "1.3"; "" on the root port */
    esp_timer_handle_t in_retry;
    uint32_t in_backoff_ms;
    bool wrote; /* an output report went out since the open */
    struct {
        uint8_t id;
        uint8_t buf[32];
        size_t len;
    } kept[KEPT_REPORTS]; /* feature reports the deck answered before its first output report */
} dev_t;

static struct {
    usb_port_cb_t cb;
    usb_host_client_handle_t client;
    dev_t devs[USB_PORT_DEVS];
    volatile uint32_t errors;
    uint32_t recoveries;
    bool auto_speed;
    volatile bool full; /* the root port runs at full speed */
    esp_timer_handle_t check;
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
    dev_t *d = t->context;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        __atomic_store_n(&d->fail_since, 0, __ATOMIC_RELAXED);
    }
    count_error(t->status);
    xSemaphoreGive(d->out_free);
}

static void in_resubmit(dev_t *d);

static void in_done(usb_transfer_t *t)
{
    dev_t *d = t->context;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0) {
        s.cb.input((int)(d - s.devs), t->data_buffer, t->actual_num_bytes, (uint64_t)esp_timer_get_time());
    }
    count_error(t->status);
    /* CANCELED while open (a flush) is queued again below */
    if (!d->open || t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        return;
    }
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        /* an error that repeats would otherwise spin the client task: wait longer each time, up to 1 s */
        if (d->in_backoff_ms == 1000) {
            ESP_LOGE(TAG, "input keeps failing (status %d): reconnecting the deck", t->status);
            s.cb.stuck((int)(d - s.devs));
        }
        d->in_backoff_ms =
            d->in_backoff_ms ? (d->in_backoff_ms * 2 > 1000 ? 1000 : d->in_backoff_ms * 2) : 100;
        if (d->in_backoff_ms < 1000) {
            ESP_LOGW(TAG, "input transfer ended with status %d", t->status);
        }
        esp_timer_start_once(d->in_retry, d->in_backoff_ms * 1000);
        return;
    }
    d->in_backoff_ms = 0;
    in_resubmit(d);
}

static void in_resubmit(dev_t *d)
{
    /* Without a pending IN transfer no key or encoder would ever arrive again. An error halts the endpoint.
     */
    if (usb_host_transfer_submit(d->in) != ESP_OK &&
        (usb_host_endpoint_clear(d->dev, d->ep_in) != ESP_OK || usb_host_transfer_submit(d->in) != ESP_OK)) {
        s.errors++;
        ESP_LOGE(TAG, "input transfer could not be queued: reconnecting the deck");
        s.cb.stuck((int)(d - s.devs));
        esp_timer_start_once(d->in_retry, 1000000); /* stuck() may hold back a replug for a while */
    }
}

static void ctrl_done(usb_transfer_t *t)
{
    xSemaphoreGive(((dev_t *)t->context)->ctrl_done);
}

static db_err_t control(dev_t *d, uint8_t type, uint8_t request, uint8_t report_id, uint8_t *data, size_t len,
                        size_t *out_len);

/* On 2026-11-08 the deck's output transfers stopped completing while it stayed attached: it showed one
 * picture for hours and the errors were only counted. Cycling the root port brought it back. */
static db_err_t write_failed(dev_t *d, db_err_t err)
{
    s.errors++;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000) | 1;
    uint32_t since = __atomic_load_n(&d->fail_since, __ATOMIC_RELAXED);
    if (!since) {
        __atomic_store_n(&d->fail_since, now, __ATOMIC_RELAXED);
    } else if (now - since >= STUCK_MS) {
        __atomic_store_n(&d->fail_since, 0, __ATOMIC_RELAXED);
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
        bool answers = control(d, 0xa1, HID_GET_REPORT, probe[0], probe, sizeof(probe), NULL) == DB_OK;
        ESP_LOGE(TAG, "deck %d took no output for %d s, its control endpoint %s: reconnecting it",
                 (int)(d - s.devs), STUCK_MS / 1000, answers ? "still answers" : "is silent too");
        s.cb.stuck((int)(d - s.devs));
    }
    return err;
}

static db_err_t io_write(const db_hid_io_t *io, const uint8_t *report, size_t len)
{
    dev_t *d = (dev_t *)io;
    if (!d->open || len > REPORT_LEN) {
        return DB_ERR_STATE;
    }
    /* A device that hangs without unplugging must not block the writer forever. */
    if (xSemaphoreTake(d->out_free, pdMS_TO_TICKS(500)) != pdTRUE) {
        return write_failed(d, DB_ERR_TIMEOUT);
    }
    usb_transfer_t *t = d->out[d->next_out];
    d->next_out = (d->next_out + 1) % OUT_SLOTS;
    d->wrote = true;
    memcpy(t->data_buffer, report, len);
    t->num_bytes = len;
    if (usb_host_transfer_submit(t) != ESP_OK) {
        xSemaphoreGive(d->out_free);
        return write_failed(d, DB_ERR_IO);
    }
    return DB_OK;
}

static db_err_t io_flush(const db_hid_io_t *io)
{
    dev_t *d = (dev_t *)io;
    for (int i = 0; i < OUT_SLOTS; i++) {
        if (xSemaphoreTake(d->out_free, pdMS_TO_TICKS(500)) != pdTRUE) {
            for (int j = 0; j < i; j++) {
                xSemaphoreGive(d->out_free);
            }
            return DB_ERR_TIMEOUT;
        }
    }
    for (int i = 0; i < OUT_SLOTS; i++) {
        xSemaphoreGive(d->out_free);
    }
    return DB_OK;
}

static db_err_t control(dev_t *d, uint8_t type, uint8_t request, uint8_t report_id, uint8_t *data, size_t len,
                        size_t *out_len)
{
    if (!d->open || len > CTRL_BUF - sizeof(usb_setup_packet_t)) {
        return DB_ERR_STATE;
    }
    xSemaphoreTake(d->ctrl_lock, portMAX_DELAY);
    usb_setup_packet_t *setup = (usb_setup_packet_t *)d->ctrl->data_buffer;
    *setup = (usb_setup_packet_t){.bmRequestType = type,
                                  .bRequest = request,
                                  .wValue = (HID_REPORT_FEATURE << 8) | report_id,
                                  .wIndex = d->intf,
                                  .wLength = (uint16_t)len};
    bool in = type & 0x80;
    if (!in) {
        memcpy(d->ctrl->data_buffer + sizeof(*setup), data, len);
    }
    d->ctrl->num_bytes = (int)(sizeof(*setup) + len);
    db_err_t res = DB_ERR_IO;
    xSemaphoreTake(d->ctrl_done, 0); /* drop a late completion of an earlier, timed out transfer */
    bool submitted = usb_host_transfer_submit_control(s.client, d->ctrl) == ESP_OK;
    bool done = submitted && xSemaphoreTake(d->ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS)) == pdTRUE;
    if (submitted && !done) {
        s.errors++;
        /* The transfer still owns the buffer; wait for it before anyone reuses d->ctrl. */
        xSemaphoreTake(d->ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS));
    }
    if (done && d->ctrl->status == USB_TRANSFER_STATUS_COMPLETED) {
        size_t got = (size_t)d->ctrl->actual_num_bytes - sizeof(*setup);
        if (in) {
            memcpy(data, d->ctrl->data_buffer + sizeof(*setup), got);
        }
        if (out_len) {
            *out_len = got;
        }
        res = DB_OK;
    }
    xSemaphoreGive(d->ctrl_lock);
    return res;
}

/* Hosts ask again for what the driver read at the open (serial, firmware, layout), also between image
 * reports, where a feature GET risks a control endpoint that hangs until the deck enumerates again. */
static db_err_t io_get_feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    dev_t *d = (dev_t *)io;
    uint8_t id = buf[0];
    if (!d->open) {
        return DB_ERR_STATE;
    }
    for (int i = 0; i < KEPT_REPORTS; i++) {
        if (d->kept[i].len && d->kept[i].id == id) {
            size_t n = d->kept[i].len < len ? d->kept[i].len : len;
            memcpy(buf, d->kept[i].buf, n);
            if (out_len) {
                *out_len = n;
            }
            return DB_OK;
        }
    }
    size_t got = 0;
    db_err_t err = control(d, 0xa1, HID_GET_REPORT, id, buf, len, &got);
    for (int i = 0; err == DB_OK && !d->wrote && got <= sizeof(d->kept[0].buf) && i < KEPT_REPORTS; i++) {
        if (!d->kept[i].len) {
            d->kept[i].id = id; /* the request's: the answer may start with another byte */
            memcpy(d->kept[i].buf, buf, got);
            d->kept[i].len = got;
            break;
        }
    }
    if (out_len) {
        *out_len = got;
    }
    return err;
}

static db_err_t io_set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    dev_t *d = (dev_t *)io;
    for (int i = 0; i < KEPT_REPORTS; i++) {
        d->kept[i].len = d->kept[i].id == buf[0] ? 0 : d->kept[i].len; /* a set makes a kept answer stale */
    }
    return control(d, 0x21, HID_SET_REPORT, buf[0], (uint8_t *)buf, len, NULL);
}

static bool any_open(void)
{
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        if (s.devs[i].dev) {
            return true;
        }
    }
    return false;
}

static void client_event(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s.cb.attached(msg->new_dev.address);
        return;
    }
    for (int i = 0; msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && i < USB_PORT_DEVS; i++) {
        dev_t *d = &s.devs[i];
        if (d->dev && msg->dev_gone.dev_hdl == d->dev) {
            d->open = false;
            s.cb.detached(i);
        }
    }
    if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && s.full && s.auto_speed) {
        /* the hub may have gone with it: check_speed goes back to high speed once the port is empty */
        esp_timer_stop(s.check);
        esp_timer_start_once(s.check, 1500000);
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
    /* A disabled port is a pulled cable to the host stack: the device goes, and comes back enumerated
     * anew. Always the root port, so behind a hub everything on it goes too: switching a hub port from here
     * (CLEAR_FEATURE on the hub) tripped an assert in esp-usb's ext_hub.c:512 and restarted the bridge. */
    ESP_LOGI(TAG, "cycling the root port");
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
        ESP_LOGE(TAG, "the root port stays off: %s", esp_err_to_name(err));
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
    dev_t *d = arg;
    if (d->open && d->in) {
        in_resubmit(d);
    }
}

/* auto: a USB device is there (a hub counts) but no deck came up at high speed, so try full speed. Back to
 * high speed once the port is empty, so a deck plugged in directly afterwards gets it. */
static void check_speed(void *arg)
{
    uint8_t addrs[8];
    int n = 0;
    if (any_open() || usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) != ESP_OK) {
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

bool usb_port_behind_hub(int dev)
{
    return s.devs[dev].dev && s.devs[dev].path[0];
}

const char *usb_port_path(int dev)
{
    return s.devs[dev].path;
}

esp_err_t usb_port_start(const usb_port_cb_t *cb, bool full_only)
{
    s.cb = *cb;
    s.auto_speed = !full_only;
    s.full = full_only;
    esp_timer_create_args_t ta = {.callback = check_speed, .name = "usb_speed"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s.check), TAG, "timer");
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        dev_t *d = &s.devs[i];
        esp_timer_create_args_t ra = {.callback = in_retry, .arg = d, .name = "usb_in_retry"};
        ESP_RETURN_ON_ERROR(esp_timer_create(&ra, &d->in_retry), TAG, "timer");
        d->out_free = xSemaphoreCreateCounting(OUT_SLOTS, OUT_SLOTS);
        d->ctrl_done = xSemaphoreCreateBinary();
        d->ctrl_lock = xSemaphoreCreateMutex();
        d->io = (db_hid_io_t){.write = io_write,
                              .flush = io_flush,
                              .get_feature = io_get_feature,
                              .set_feature = io_set_feature};
        if (!d->out_free || !d->ctrl_done || !d->ctrl_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 1) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    usb_host_client_config_t cc = {
        .max_num_event_msg = 5,
        .async = {.client_event_callback = client_event},
    };
    ESP_RETURN_ON_ERROR(usb_host_client_register(&cc, &s.client), TAG, "client register");
    for (int i = 0; i < USB_PORT_DEVS; i++) {
        ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(CTRL_BUF, 0, &s.devs[i].ctrl), TAG, "ctrl alloc");
        s.devs[i].ctrl->callback = ctrl_done;
        s.devs[i].ctrl->context = &s.devs[i];
    }
    if (xTaskCreatePinnedToCore(client_task, "usb_client", 4096, NULL, 9, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (s.auto_speed) {
        esp_timer_start_once(s.check, 6000000);
    }
    return ESP_OK;
}

/* First HID interface with an interrupt IN and OUT endpoint. */
static esp_err_t find_endpoints(dev_t *d, const usb_config_desc_t *cd)
{
    int off = 0;
    for (int i = 0; i < cd->bNumInterfaces; i++) {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cd, (uint8_t)i, 0, &off);
        if (!intf || intf->bInterfaceClass != HID_CLASS) {
            continue;
        }
        d->ep_in = d->ep_out = 0;
        for (int e = 0; e < intf->bNumEndpoints; e++) {
            int eo = off;
            const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(intf, e, cd->wTotalLength, &eo);
            if (!ep) {
                continue;
            }
            if (ep->bEndpointAddress & 0x80) {
                /* IN sizes must be a multiple of the packet size; 512 also holds the Mini's 65 byte report */
                int in_len = USB_EP_DESC_GET_MPS(ep) > 512 ? USB_EP_DESC_GET_MPS(ep) : 512;
                if (!d->in && usb_host_transfer_alloc(in_len, 0, &d->in) != ESP_OK) {
                    return ESP_ERR_NO_MEM;
                }
                d->ep_in = ep->bEndpointAddress;
                d->in->num_bytes = in_len;
            } else {
                d->ep_out = ep->bEndpointAddress;
            }
        }
        if (d->ep_in && d->ep_out) {
            d->intf = intf->bInterfaceNumber;
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

static void free_transfers(dev_t *d)
{
    for (int i = 0; i < OUT_SLOTS; i++) {
        if (d->out[i]) {
            usb_host_transfer_free(d->out[i]);
            d->out[i] = NULL;
        }
    }
    if (d->in) {
        usb_host_transfer_free(d->in);
        d->in = NULL;
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

static void usb_string(int dev, bool serial, char *out, size_t cap)
{
    usb_device_info_t info;
    const usb_str_desc_t *d = NULL;
    if (s.devs[dev].dev && usb_host_device_info(s.devs[dev].dev, &info) == ESP_OK) {
        d = serial ? info.str_desc_serial_num : info.str_desc_product;
    }
    size_t n = 0;
    for (int i = 0; d && i < (d->bLength - 2) / 2 && n + 1 < cap; i++) {
        out[n++] = d->wData[i] < 0x80 ? (char)d->wData[i] : '?';
    }
    out[n] = '\0';
}

void usb_port_product(int dev, char *out, size_t cap)
{
    usb_string(dev, false, out, cap);
}

void usb_port_serial(int dev, char *out, size_t cap)
{
    usb_string(dev, true, out, cap);
}

/* Hub ports from the root port down, "1.3": a deck without a serial number is told apart by where it hangs.
 */
static void hub_path(usb_device_handle_t dev, char *out, size_t cap)
{
    uint8_t ports[7];
    int n = 0;
    usb_device_info_t info;
    while (n < 7 && usb_host_device_info(dev, &info) == ESP_OK && info.parent.dev_hdl) {
        ports[n++] = info.parent.port_num;
        dev = info.parent.dev_hdl;
    }
    size_t len = 0;
    out[0] = '\0';
    while (n-- > 0 && len < cap) {
        len += (size_t)snprintf(out + len, cap - len, len ? ".%u" : "%u", ports[n]);
    }
}

esp_err_t usb_port_open(uint8_t address, int *index, uint16_t *vid, uint16_t *pid)
{
    dev_t *d = NULL;
    for (int i = 0; i < USB_PORT_DEVS && !d; i++) {
        d = s.devs[i].dev ? NULL : &s.devs[i];
    }
    if (!d) {
        ESP_LOGW(TAG, "device %u ignored: %d decks are open", address, USB_PORT_DEVS);
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(usb_host_device_open(s.client, address, &d->dev), TAG, "open");
    const usb_device_desc_t *dd;
    const usb_config_desc_t *cd;
    usb_device_info_t info;
    usb_host_get_device_descriptor(d->dev, &dd);
    usb_host_get_active_config_descriptor(d->dev, &cd);
    usb_host_device_info(d->dev, &info);
    hub_path(d->dev, d->path, sizeof(d->path));
    *vid = dd->idVendor;
    *pid = dd->idProduct;
    ESP_LOGI(TAG, "device %04x:%04x, %s speed, port %s", *vid, *pid,
             info.speed == USB_SPEED_HIGH ? "high" : "full", d->path[0] ? d->path : "root");
    esp_err_t err = find_endpoints(d, cd);
    if (info.speed != USB_SPEED_HIGH) {
        clamp_fs_out_mps(cd);
    }
    if (err == ESP_OK) {
        err = usb_host_interface_claim(s.client, d->dev, d->intf, 0);
    }
    for (int i = 0; err == ESP_OK && i < OUT_SLOTS; i++) {
        err = usb_host_transfer_alloc(REPORT_LEN, 0, &d->out[i]);
        if (err == ESP_OK) {
            d->out[i]->device_handle = d->dev;
            d->out[i]->bEndpointAddress = d->ep_out;
            d->out[i]->callback = out_done;
            d->out[i]->context = d;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no usable HID interface: %s", esp_err_to_name(err));
        if (s.auto_speed && !any_open()) { /* a dock's own device came up; the deck behind it may not */
            esp_timer_stop(s.check);
            esp_timer_start_once(s.check, 3000000);
        }
        free_transfers(d);
        usb_host_device_close(s.client, d->dev);
        d->dev = NULL;
        return err;
    }
    d->ctrl->device_handle = d->dev;
    d->in->device_handle = d->dev;
    d->in->bEndpointAddress = d->ep_in;
    d->in->callback = in_done;
    d->in->context = d;
    d->io.out_report_size = REPORT_LEN;
    d->next_out = 0;
    d->fail_since = 0;
    d->open = true;
    d->in_backoff_ms = 0;
    d->wrote = false;
    memset(d->kept, 0, sizeof(d->kept));
    *index = (int)(d - s.devs);
    in_resubmit(d);
    return ESP_OK;
}

void usb_port_close(int dev)
{
    dev_t *d = &s.devs[dev];
    d->open = false;
    if (!d->dev) {
        return;
    }
    /* Let every transfer come back before the endpoints go away. */
    io_flush(&d->io);
    usb_host_endpoint_halt(d->dev, d->ep_in);
    usb_host_endpoint_flush(d->dev, d->ep_in);
    vTaskDelay(pdMS_TO_TICKS(20));
    usb_host_interface_release(s.client, d->dev, d->intf);
    free_transfers(d);
    usb_host_device_close(s.client, d->dev);
    d->dev = NULL;
}

const db_hid_io_t *usb_port_io(int dev)
{
    return &s.devs[dev].io;
}

uint32_t usb_port_errors(void)
{
    return s.errors;
}

uint32_t usb_port_recoveries(void)
{
    return s.recoveries;
}

esp_err_t usb_port_stall(int dev)
{
    dev_t *d = &s.devs[dev];
    return d->open ? usb_host_endpoint_halt(d->dev, d->ep_out) : ESP_ERR_INVALID_STATE;
}
