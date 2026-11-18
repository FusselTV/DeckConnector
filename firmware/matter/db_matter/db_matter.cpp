#include <string.h>

#include <app/server/Server.h>
#include <crypto/CHIPCryptoPAL.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

extern "C" {
#include "db_config.h"
#include "db_deck.h"
#include "db_log.h"
#include "db_matter.h"
#include "db_ui.h"
}
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qrcode.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter";

#define PART  "matter"
#define NS    "deckconnector"
#define ITERS 1000

/* Not in the partition table, which an update does not change: the free flash between phy_init and ota_0. */
#define PART_OFFSET 0x12000
#define PART_SIZE   0xE000

typedef struct {
    db_matter_slot_t id;
    uint8_t keys, dials;
    char model[24];
} slot_t;

typedef struct {
    int8_t slot;
    int8_t input; /* keys, then dials; -1: a deck came or went */
    bool down;
} msg_t;

static struct {
    uint32_t pin;
    uint16_t disc;
    uint8_t salt[32];
    uint8_t verifier[chip::Crypto::kSpake2p_VerifierSerialized_Length];
    slot_t slots[DB_MAX_DECKS];
    bool live[DB_MAX_DECKS]; /* its endpoints exist */
    db_sw_t sw[DB_MAX_DECKS][DB_MATTER_KEYS + DB_MATTER_DIALS];
    endpoint_t *aggregator;
    QueueHandle_t q;
    bool loaded, on, started;
    int fabrics;
    bool window;
} s;

class Commissionable : public chip::DeviceLayer::CommissionableDataProvider
{
    CHIP_ERROR GetSetupDiscriminator(uint16_t &d) override
    {
        d = s.disc;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupDiscriminator(uint16_t) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetSpake2pIterationCount(uint32_t &n) override
    {
        n = ITERS;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &out) override
    {
        return chip::CopySpanToMutableSpan(chip::ByteSpan(s.salt), out);
    }
    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &out, size_t &len) override
    {
        len = sizeof(s.verifier);
        return chip::CopySpanToMutableSpan(chip::ByteSpan(s.verifier), out);
    }
    CHIP_ERROR GetSetupPasscode(uint32_t &p) override
    {
        p = s.pin;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR SetSetupPasscode(uint32_t) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
};

static Commissionable commissionable;

static esp_err_t open_part(void)
{
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PART);
    if (!p) {
        esp_err_t err = esp_partition_register_external(
            NULL, PART_OFFSET, PART_SIZE, PART, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, &p);
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = nvs_flash_init_partition(PART);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase_partition(PART);
        err = err == ESP_OK ? nvs_flash_init_partition(PART) : err;
    }
    return err;
}

static void save_slots(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "slots", s.slots, sizeof(s.slots));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* The pairing code stays until a reset; on/off and the decks seen too. */
static esp_err_t load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(PART, NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t off = 0;
    nvs_get_u8(h, "off", &off);
    s.on = !off;
    size_t len = sizeof(s.slots);
    if (nvs_get_blob(h, "slots", s.slots, &len) != ESP_OK || len != sizeof(s.slots)) {
        memset(s.slots, 0, sizeof(s.slots));
    }
    len = sizeof(s.salt);
    if (nvs_get_u32(h, "pin", &s.pin) != ESP_OK || nvs_get_u16(h, "disc", &s.disc) != ESP_OK ||
        nvs_get_blob(h, "salt", s.salt, &len) != ESP_OK) {
        do {
            s.pin = esp_random() % 99999998 + 1;
        } while (!chip::SetupPayload::IsValidSetupPIN(s.pin));
        s.disc = esp_random() & 0xFFF;
        esp_fill_random(s.salt, sizeof(s.salt));
        nvs_set_u32(h, "pin", s.pin);
        nvs_set_u16(h, "disc", s.disc);
        nvs_set_blob(h, "salt", s.salt, sizeof(s.salt));
    }
    nvs_commit(h);
    nvs_close(h);
    /* above every endpoint of ours: esp_matter resumes only ids below it */
    if (nvs_open_from_partition(PART, "esp_matter_kvs", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, "min_uu_ep_id", 0xFF40);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

static void payload(char *qr, size_t qr_cap, char *code, size_t code_cap)
{
    chip::PayloadContents p;
    p.vendorID = CONFIG_DEVICE_VENDOR_ID;
    p.productID = CONFIG_DEVICE_PRODUCT_ID;
    p.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kOnNetwork);
    p.discriminator.SetLongValue(s.disc);
    p.setUpPINCode = s.pin;
    chip::MutableCharSpan q(qr, qr_cap - 1), c(code, code_cap - 1);
    qr[0] = code[0] = 0;
    if (chip::QRCodeBasicSetupPayloadGenerator(p).payloadBase38Representation(q) == CHIP_NO_ERROR) {
        qr[q.size()] = 0;
    }
    if (chip::ManualSetupPayloadGenerator(p).payloadDecimalStringRepresentation(c) == CHIP_NO_ERROR) {
        code[c.size()] = 0;
    }
}

static endpoint_t *child(uint16_t id, endpoint_t *parent)
{
    endpoint_t *ep = endpoint::resume(node::get(), ENDPOINT_FLAG_DESTROYABLE, id, nullptr);
    if (!ep) {
        return nullptr;
    }
    endpoint::generic_switch::config_t cfg;
    namespace f = cluster::switch_cluster::feature;
    cfg.switch_cluster.feature_flags = f::momentary_switch::get_id() | f::momentary_switch_release::get_id() |
                                       f::momentary_switch_long_press::get_id() |
                                       f::momentary_switch_multi_press::get_id();
    cfg.switch_cluster.features.momentary_switch_multi_press.multi_press_max = DB_SW_MAX;
    endpoint::generic_switch::add(ep, &cfg);
    endpoint::set_parent_endpoint(ep, parent);
    endpoint::enable(ep);
    return ep;
}

static void set_reachable(uint16_t ep, bool on)
{
    esp_matter_attr_val_t v = esp_matter_bool(on);
    attribute::update(ep, BridgedDeviceBasicInformation::Id,
                      BridgedDeviceBasicInformation::Attributes::Reachable::Id, &v);
}

static uint16_t node_ep(int slot)
{
    return (uint16_t)(64 * (1 + s.slots[slot].id.gen * DB_MAX_DECKS + slot));
}

static void drop(int slot)
{
    uint16_t base = node_ep(slot);
    for (uint16_t id = base + 1; id < base + 1 + DB_MATTER_KEYS + DB_MATTER_DIALS; id++) {
        endpoint_t *ep = endpoint::get(node::get(), id);
        if (ep) {
            endpoint::destroy(node::get(), ep);
        }
    }
    endpoint_t *n = endpoint::get(node::get(), base);
    if (n) {
        endpoint::destroy(node::get(), n);
    }
    s.live[slot] = false;
}

/* Under the stack lock */
static void build(int slot, bool reachable)
{
    slot_t *k = &s.slots[slot];
    uint16_t base = node_ep(slot);
    endpoint_t *n =
        endpoint::resume(node::get(), ENDPOINT_FLAG_DESTROYABLE | ENDPOINT_FLAG_BRIDGE, base, nullptr);
    if (!n) {
        return;
    }
    endpoint::bridged_node::config_t cfg;
    strlcpy(cfg.bridged_device_basic_information.unique_id, k->id.serial,
            sizeof(cfg.bridged_device_basic_information.unique_id));
    cfg.bridged_device_basic_information.reachable = reachable;
    endpoint::bridged_node::add(n, &cfg);
    cluster_t *info = cluster::get(n, BridgedDeviceBasicInformation::Id);
    char label[48];
    snprintf(label, sizeof(label), "%s %d", k->model, slot + 1);
    cluster::bridged_device_basic_information::attribute::create_node_label(info, label, strlen(label));
    cluster::bridged_device_basic_information::attribute::create_product_name(info, k->model,
                                                                              strlen(k->model));
    cluster::bridged_device_basic_information::attribute::create_serial_number(info, k->id.serial,
                                                                               strlen(k->id.serial));
    endpoint::set_parent_endpoint(n, s.aggregator);
    endpoint::enable(n);
    for (int i = 0; i < k->keys; i++) {
        child(base + 1 + i, n);
    }
    for (int i = 0; i < k->dials; i++) {
        child(base + 1 + DB_MATTER_KEYS + i, n);
    }
    s.live[slot] = true;
}

static void deck_changed(int slot)
{
    db_device_info_t info;
    bool present = db_deck_info(slot, &info);
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    if (!present) {
        if (s.live[slot]) {
            set_reachable(node_ep(slot), false);
        }
        memset(s.sw[slot], 0, sizeof(s.sw[slot]));
        return;
    }
    slot_t old = s.slots[slot];
    bool moved;
    db_matter_node_ep(&s.slots[slot].id, slot, info.serial, &moved);
    slot_t *k = &s.slots[slot];
    k->keys = info.key_count < DB_MATTER_KEYS ? info.key_count : DB_MATTER_KEYS;
    k->dials = info.encoder_count < DB_MATTER_DIALS ? info.encoder_count : DB_MATTER_DIALS;
    strlcpy(k->model, info.model, sizeof(k->model));
    if (moved && s.live[slot]) {
        slot_t now = *k;
        *k = old;
        drop(slot);
        *k = now;
    }
    if (memcmp(&old, k, sizeof(old)) != 0) {
        save_slots();
    }
    if (s.live[slot]) {
        set_reachable(node_ep(slot), true);
    } else {
        build(slot, true);
    }
}

static void emit(uint16_t ep, const db_sw_out_t *o)
{
    esp_matter_attr_val_t pos = esp_matter_uint8(
        o->event == DB_SW_INITIAL_PRESS || o->event == DB_SW_LONG_PRESS || o->event == DB_SW_MULTI_ONGOING);
    switch (o->event) {
    case DB_SW_INITIAL_PRESS:
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &pos);
        cluster::switch_cluster::event::send_initial_press(ep, 1);
        break;
    case DB_SW_LONG_PRESS:
        cluster::switch_cluster::event::send_long_press(ep, 1);
        break;
    case DB_SW_SHORT_RELEASE:
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &pos);
        cluster::switch_cluster::event::send_short_release(ep, 1);
        break;
    case DB_SW_LONG_RELEASE:
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &pos);
        cluster::switch_cluster::event::send_long_release(ep, 1);
        break;
    case DB_SW_MULTI_ONGOING:
        cluster::switch_cluster::event::send_multi_press_ongoing(ep, 1, o->count);
        break;
    case DB_SW_MULTI_COMPLETE:
        cluster::switch_cluster::event::send_multi_press_complete(ep, 1, o->count);
        break;
    }
}

static int64_t tick(void)
{
    int64_t next = 0;
    int64_t now = esp_timer_get_time();
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        for (int i = 0; i < DB_MATTER_KEYS + DB_MATTER_DIALS; i++) {
            db_sw_out_t o;
            if (db_sw_tick(&s.sw[d][i], now, &o)) {
                lock::ScopedChipStackLock lock(portMAX_DELAY);
                emit((uint16_t)(node_ep(d) + 1 + i), &o);
            }
            int64_t due = db_sw_due(&s.sw[d][i]);
            if (due && (!next || due < next)) {
                next = due;
            }
        }
    }
    return next;
}

/* Its own task: the stack lock can be held for a while (a new session), the deck's event task must not wait.
 */
static void task(void *arg)
{
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (s.slots[d].id.serial[0]) {
            lock::ScopedChipStackLock lock(portMAX_DELAY);
            build(d, false);
        }
        deck_changed(d);
    }
    int64_t next = 0;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (next) {
            int64_t us = next - esp_timer_get_time();
            wait = us > 0 ? pdMS_TO_TICKS(us / 1000 + 1) : 0;
        }
        msg_t m;
        if (xQueueReceive(s.q, &m, wait) == pdTRUE) {
            if (m.input < 0) {
                deck_changed(m.slot);
            } else if (s.live[m.slot]) {
                db_sw_out_t out[2];
                int n = db_sw_input(&s.sw[m.slot][m.input], m.down, esp_timer_get_time(), out);
                lock::ScopedChipStackLock lock(portMAX_DELAY);
                for (int e = 0; e < n; e++) {
                    emit((uint16_t)(node_ep(m.slot) + 1 + m.input), &out[e]);
                }
            }
        }
        next = tick();
    }
}

static void on_input(int deck, const db_input_t *ev)
{
    /* never a press sent over the network, not even one a person sent at the web page */
    if (!db_deck_input_from_deck()) {
        return;
    }
    int at = db_matter_input_ep(0, ev);
    if (at) {
        msg_t m = {.slot = (int8_t)deck,
                   .input = (int8_t)(at - 1),
                   .down = ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_ENCODER_DOWN};
        xQueueSend(s.q, &m, 0);
    }
}

static void on_device(int deck, const db_device_info_t *info)
{
    msg_t m = {.slot = (int8_t)deck, .input = -1, .down = false};
    xQueueSend(s.q, &m, pdMS_TO_TICKS(1000));
}

static void on_event(const ChipDeviceEvent *e, intptr_t arg)
{
    switch (e->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        db_log_event(TAG, "matter", "a controller paired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        db_log_event(TAG, "matter", "a controller left");
        break;
    default:
        break;
    }
    s.fabrics = chip::Server::GetInstance().GetFabricTable().FabricCount();
    s.window = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
}

/* Matter talks IPv6; the bridge's own interfaces have only IPv4 until asked. */
static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_netif_create_ip6_linklocal(((ip_event_got_ip_t *)data)->esp_netif);
}

void db_matter_start(void)
{
    if (!s.loaded) {
        esp_err_t err = open_part();
        err = err == ESP_OK ? load() : err;
        if (err != ESP_OK) {
            db_log_event(TAG, "matter", "storage failed: %s", esp_err_to_name(err));
            return;
        }
        s.loaded = true;
    }
    if (!s.on || s.started) {
        return;
    }
    chip::Crypto::Spake2pVerifier v;
    chip::MutableByteSpan vs(s.verifier);
    if (v.Generate(ITERS, chip::ByteSpan(s.salt), s.pin) != CHIP_NO_ERROR ||
        v.Serialize(vs) != CHIP_NO_ERROR) {
        db_log_event(TAG, "matter", "no pairing code");
        return;
    }
    set_custom_commissionable_data_provider(&commissionable);

    node::config_t cfg;
    strlcpy(cfg.root_node.basic_information.node_label, db_config()->name,
            sizeof(cfg.root_node.basic_information.node_label));
    node_t *n = node::create(&cfg, nullptr, nullptr);
    endpoint::aggregator::config_t agg;
    s.aggregator = n ? endpoint::aggregator::create(n, &agg, ENDPOINT_FLAG_NONE, nullptr) : nullptr;
    s.q = xQueueCreate(32, sizeof(msg_t));
    if (!s.aggregator || !s.q) {
        db_log_event(TAG, "matter", "out of memory");
        return;
    }
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, nullptr);
    for (const char *key : {"ETH_DEF", "WIFI_STA_DEF"}) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
        if (netif && esp_netif_is_netif_up(netif)) {
            esp_netif_create_ip6_linklocal(netif);
        }
    }
    esp_err_t err = esp_matter::start(on_event);
    if (err != ESP_OK) {
        db_log_event(TAG, "matter", "failed to start: %s", esp_err_to_name(err));
        return;
    }
    {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        s.fabrics = chip::Server::GetInstance().GetFabricTable().FabricCount();
        s.window = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    }
    s.started = true;
    db_deck_listener_t l = {.device_changed = on_device, .input = on_input, .external = true};
    db_deck_add_listener(&l);
    xTaskCreate(task, "matter_in", 4096, nullptr, 5, nullptr);
}

void db_matter_status(db_json_t *j)
{
    db_json_obj_open(j, "matter");
    db_json_bool(j, "on", s.on);
    db_json_bool(j, "running", s.started);
    db_json_int(j, "controllers", s.fabrics);
    db_json_bool(j, "pairing", s.window);
    db_json_obj_close(j);
}

static char *qr_hex;

static void take_qr(esp_qrcode_handle_t q)
{
    int n = esp_qrcode_get_size(q);
    uint8_t bits[(41 * 41 + 7) / 8] = {0};
    if (n > 41) {
        return;
    }
    for (int i = 0; i < n * n; i++) {
        bits[i / 8] |= (uint8_t)(esp_qrcode_get_module(q, i % n, i / n) << (i % 8));
    }
    char *o = qr_hex + sprintf(qr_hex, "%d:", n);
    for (int i = 0; i < (n * n + 7) / 8; i++) {
        o += sprintf(o, "%02x", bits[i]);
    }
}

void db_matter_codes(db_json_t *j)
{
    char qr[32], code[16];
    payload(qr, sizeof(qr), code, sizeof(code));
    db_json_str(j, "qr", qr);
    db_json_str(j, "code", code);
    static char hex[4 + 2 * (41 * 41 + 7) / 8 + 1];
    static SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    xSemaphoreTake(lock, portMAX_DELAY);
    qr_hex = hex;
    hex[0] = 0;
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = take_qr;
    cfg.max_qrcode_version = 6;
    esp_log_level_set("QRCODE", ESP_LOG_WARN); /* it logs the text: the pairing code */
    esp_qrcode_generate(&cfg, qr);
    db_json_str(j, "qr_bits", hex);
    xSemaphoreGive(lock);
}

void db_matter_set_on(bool on)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "off", !on);
        nvs_commit(h);
        nvs_close(h);
    }
    s.on = on;
    if (on && !s.started) {
        db_matter_start();
    }
}

int db_matter_show(void)
{
    char qr[32], code[16];
    payload(qr, sizeof(qr), code, sizeof(code));
    return s.started ? db_ui_show_matter(qr, code) : 0;
}

bool db_matter_open_window(void)
{
    if (!s.started) {
        return false;
    }
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &w = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (!w.IsCommissioningWindowOpen() &&
        w.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(15 * 60)) != CHIP_NO_ERROR) {
        return false;
    }
    s.window = true;
    return true;
}

esp_err_t db_matter_erase(void)
{
    if (s.started) {
        chip::DeviceLayer::PlatformMgr()
            .LockChipStack(); /* kept: the stack writes nothing more before the restart */
    }
    nvs_flash_deinit_partition(PART);
    return nvs_flash_erase_partition(PART);
}
