#include <string.h>

#include <app/server/Server.h>
#include <crypto/CHIPCryptoPAL.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <new>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInfoProvider.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

extern "C" {
#include "db_config.h"
#include "db_deck.h"
#include "db_dock.h"
#include "db_log.h"
#include "db_matter.h"
#include "db_ota.h"
#include "db_ptable.h"
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

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter";

#define PART  "matter"
#define NS    "deckconnector"
#define ITERS 1000

typedef struct {
    db_matter_slot_t id;
    uint8_t keys, dials;
    char model[24];
} slot_t;

typedef struct {
    int8_t slot;
    int8_t input; /* keys, then dials; -1: a deck came or went */
    bool down;
    bool covered;
} msg_t;

static struct {
    uint32_t pin;
    uint16_t disc;
    uint8_t salt[32];
    uint8_t verifier[chip::Crypto::kSpake2p_VerifierSerialized_Length];
    slot_t slots[DB_MAX_DECKS];
    bool live[DB_MAX_DECKS];          /* its endpoints exist */
    uint8_t image_keys[DB_MAX_DECKS]; /* 0 until the deck is seen after the start */
    db_sw_deck_t sw[DB_MAX_DECKS];
    endpoint_t *aggregator;
    QueueHandle_t q;
    SemaphoreHandle_t start_lock;
    bool loaded, on, started;
    bool failed; /* after node::create: another try would make a second node */
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

static uint16_t node_ep(int slot);

/* Home Assistant names an entity after the FixedLabel "ha_entitylabel", for test vendor IDs only */
class Labels : public chip::DeviceLayer::DeviceInfoProvider
{
    class One : public FixedLabelIterator
    {
      public:
        char value[17];
        bool done = false;
        size_t Count() override
        {
            return 1;
        }
        bool Next(FixedLabelType &item) override
        {
            if (done) {
                return false;
            }
            item.label = chip::CharSpan::fromCharString("ha_entitylabel");
            item.value = chip::CharSpan::fromCharString(value);
            done = true;
            return true;
        }
        void Release() override
        {
            delete this;
        }
    };

    FixedLabelIterator *IterateFixedLabel(chip::EndpointId ep) override
    {
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            char value[17];
            if (s.live[d] && db_matter_label(node_ep(d), ep, s.image_keys[d], db_config()->lang, value)) {
                One *it = new (std::nothrow) One;
                if (it) {
                    strlcpy(it->value, value, sizeof(it->value));
                }
                return it;
            }
        }
        return nullptr;
    }
    UserLabelIterator *IterateUserLabel(chip::EndpointId) override
    {
        return nullptr;
    }
    SupportedLocalesIterator *IterateSupportedLocales() override
    {
        return nullptr;
    }
    SupportedCalendarTypesIterator *IterateSupportedCalendarTypes() override
    {
        return nullptr;
    }
    CHIP_ERROR SetUserLabelAt(chip::EndpointId, size_t, const UserLabelType &) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR DeleteUserLabelAt(chip::EndpointId, size_t) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR SetUserLabelLength(chip::EndpointId, size_t) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetUserLabelLength(chip::EndpointId, size_t &) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
};

static Labels labels;

static esp_err_t open_part(void)
{
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PART);
    if (!p) {
        if (!db_ota_spare_free()) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t err =
            esp_partition_register_external(NULL, DB_SPARE_OFFSET, DB_SPARE_SIZE, PART,
                                            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, &p);
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

/* The pairing code stays until a reset; the decks seen too. */
static esp_err_t load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(PART, NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
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
    cfg.identify.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kNone);
    namespace f = cluster::switch_cluster::feature;
    cfg.switch_cluster.feature_flags = f::momentary_switch::get_id() | f::momentary_switch_release::get_id() |
                                       f::momentary_switch_long_press::get_id();
    /* add() leaves out the descriptor, without which a controller sees no endpoint */
    cluster::descriptor::create(ep, &cfg.descriptor, CLUSTER_FLAG_SERVER);
    endpoint::generic_switch::add(ep, &cfg);
    cluster::fixed_label::config_t fixed;
    cluster::fixed_label::create(ep, &fixed, CLUSTER_FLAG_SERVER);
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
    return db_matter_ep(s.slots[slot].id.gen, slot);
}

static void drop(int slot, uint16_t base)
{
    for (uint16_t id = base + 1; id <= base + DB_MATTER_INPUTS; id++) {
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

/* The model, with the slot number as the web UI counts once there are several decks */
static void node_label(int slot, char *out, size_t cap)
{
    int decks = 0;
    for (const slot_t &o : s.slots) {
        decks += o.id.serial[0] != 0;
    }
    if (decks > 1) {
        snprintf(out, cap, "%s %d", s.slots[slot].model, slot + 1);
    } else {
        strlcpy(out, s.slots[slot].model, cap);
    }
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
    cluster::descriptor::create(n, &cfg.descriptor, CLUSTER_FLAG_SERVER);
    endpoint::bridged_node::add(n, &cfg);
    cluster_t *info = cluster::get(n, BridgedDeviceBasicInformation::Id);
    char label[48];
    node_label(slot, label, sizeof(label));
    cluster::bridged_device_basic_information::attribute::create_node_label(info, label, strlen(label));
    /* Home Assistant reads the vendor for its label list here, not on the root node */
    cluster::bridged_device_basic_information::attribute::create_vendor_id(info, CONFIG_DEVICE_VENDOR_ID);
    cluster::bridged_device_basic_information::attribute::create_product_id(info, CONFIG_DEVICE_PRODUCT_ID);
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

static void emit(uint16_t ep, db_sw_event_t e);

static void deck_changed(int slot)
{
    db_device_info_t info;
    bool present = db_deck_info(slot, &info);
    lock::ScopedChipStackLock lock(portMAX_DELAY);
    if (!present) {
        db_sw_event_t gone[DB_MATTER_INPUTS];
        db_sw_deck_gone(&s.sw[slot], esp_timer_get_time(), gone);
        for (int i = 0; i < DB_MATTER_INPUTS && s.live[slot]; i++) {
            if (gone[i] != DB_SW_NONE) {
                emit((uint16_t)(node_ep(slot) + 1 + i), gone[i]);
            }
        }
        if (s.live[slot]) {
            set_reachable(node_ep(slot), false);
        }
        return;
    }
    slot_t old = s.slots[slot];
    uint16_t old_base = node_ep(slot);
    bool moved;
    db_matter_node_ep(&s.slots[slot].id, slot, info.serial, &moved);
    slot_t *k = &s.slots[slot];
    k->keys = info.key_count < DB_MATTER_KEYS ? info.key_count : DB_MATTER_KEYS;
    k->dials = info.encoder_count < DB_MATTER_DIALS ? info.encoder_count : DB_MATTER_DIALS;
    strlcpy(k->model, info.model, sizeof(k->model));
    s.image_keys[slot] = db_device_image_keys(&info);
    /* a deck without a serial is named by its port (db_deck_identity): another model there is another deck.
     * Not for decks with one: a model renamed in our driver would move their endpoints. */
    if (!moved && strncmp(k->id.serial, "port-", 5) == 0 &&
        (k->keys != old.keys || k->dials != old.dials || strcmp(k->model, old.model) != 0)) {
        k->id.gen = (uint8_t)((k->id.gen + 1) % DB_MATTER_GENS);
        moved = true;
    }
    if (moved && s.live[slot]) {
        drop(slot, old_base);
    }
    if (memcmp(&old, k, sizeof(old)) != 0) {
        save_slots();
    }
    if (!old.id.serial[0]) { /* one deck more: the others' names may gain their number */
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            if (d != slot && s.live[d]) {
                char label[48];
                node_label(d, label, sizeof(label));
                esp_matter_attr_val_t v = esp_matter_char_str(label, (uint16_t)strlen(label));
                attribute::update(node_ep(d), BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, &v);
            }
        }
    }
    if (s.live[slot]) {
        set_reachable(node_ep(slot), true);
    } else {
        build(slot, true);
    }
}

static void emit(uint16_t ep, db_sw_event_t e)
{
    esp_matter_attr_val_t pos = esp_matter_uint8(e == DB_SW_INITIAL_PRESS || e == DB_SW_LONG_PRESS);
    if (e != DB_SW_LONG_PRESS) {
        attribute::update(ep, Switch::Id, Switch::Attributes::CurrentPosition::Id, &pos);
    }
    switch (e) {
    case DB_SW_INITIAL_PRESS:
        cluster::switch_cluster::event::send_initial_press(ep, 1);
        break;
    case DB_SW_LONG_PRESS:
        cluster::switch_cluster::event::send_long_press(ep, 1);
        break;
    case DB_SW_SHORT_RELEASE:
        cluster::switch_cluster::event::send_short_release(ep, 1);
        break;
    case DB_SW_LONG_RELEASE:
        cluster::switch_cluster::event::send_long_release(ep, 1);
        break;
    case DB_SW_NONE:
        break;
    }
}

static const int *chord(int slot)
{
    static int c[2];
    bool on_dials;
    int last;
    const slot_t *k = &s.slots[slot];
    db_device_info_t dev;
    if (!db_deck_info(slot, &dev) || !db_ui_menu_chord(&dev, NULL, &on_dials, &last) ||
        last >= (on_dials ? k->dials : k->keys)) {
        c[0] = c[1] = -1;
    } else {
        c[0] = on_dials ? DB_MATTER_KEYS : 0;
        c[1] = c[0] + last;
    }
    return c;
}

static int64_t tick(void)
{
    int64_t next = 0;
    int64_t now = esp_timer_get_time();
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        for (int i = 0; i < DB_MATTER_INPUTS; i++) {
            db_sw_event_t e = db_sw_deck_tick(&s.sw[d], i, now);
            if (e != DB_SW_NONE) {
                lock::ScopedChipStackLock lock(portMAX_DELAY);
                emit((uint16_t)(node_ep(d) + 1 + i), e);
            }
            int64_t due = db_sw_due(&s.sw[d].sw[i]);
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
                db_sw_event_t e = db_sw_deck_input(&s.sw[m.slot], m.input, m.down, m.covered, chord(m.slot),
                                                   esp_timer_get_time());
                if (e != DB_SW_NONE) {
                    lock::ScopedChipStackLock lock(portMAX_DELAY);
                    emit((uint16_t)(node_ep(m.slot) + 1 + m.input), e);
                }
            }
        }
        next = tick();
    }
}

static void queue_input(int deck, const db_input_t *ev, db_matter_from_t from)
{
    int at = db_matter_input_ep(0, ev);
    if (db_matter_takes(from) && at) {
        char ip[16];
        msg_t m = {.slot = (int8_t)deck,
                   .input = (int8_t)(at - 1),
                   .down = ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_ENCODER_DOWN,
                   .covered = db_ui_menu_open() || db_dock_request(ip)};
        xQueueSend(s.q, &m, pdMS_TO_TICKS(20)); /* a lost release would leave the switch held */
    }
}

static void on_input(int deck, const db_input_t *ev)
{
    queue_input(deck, ev, db_deck_input_from_deck() ? DB_MATTER_FROM_DECK : DB_MATTER_FROM_API);
}

bool db_matter_debug_input(int deck, const db_input_t *ev)
{
    if (!s.started || deck < 0 || deck >= DB_MAX_DECKS) {
        return false;
    }
    queue_input(deck, ev, DB_MATTER_FROM_DEBUG);
    return true;
}

static void on_device(int deck, const db_device_info_t *info)
{
    msg_t m = {.slot = (int8_t)deck, .input = -1, .down = false, .covered = false};
    xQueueSend(s.q, &m, pdMS_TO_TICKS(1000));
}

/* Under the stack lock */
static void refresh(void)
{
    s.fabrics = chip::Server::GetInstance().GetFabricTable().FabricCount();
    s.window = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
}

static void on_event(const ChipDeviceEvent *e, intptr_t arg)
{
    switch (e->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        db_log_event_args(TAG, DB_EV_MATTER_PAIRED, nullptr);
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        db_log_event_args(TAG, DB_EV_MATTER_LEFT, nullptr);
        break;
    default:
        break;
    }
    refresh();
}

static void restart_mdns(chip::System::Layer *, void *)
{
    chip::app::DnssdServer::Instance().StartServer();
}

/* Matter talks IPv6; the bridge's own interfaces have only IPv4 until asked. CHIP restarts its mDNS on new
 * Ethernet addresses, not on Wi-Fi ones (its station is off), so a board on Wi-Fi only needs this. Once per
 * burst, a second after the last address: a restart keeps the old sockets until their queued packets are
 * read, and restarts back to back ran CHIP's UDP pool dry and left no mDNS at all.
 */
static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_netif_t *netif = id == IP_EVENT_GOT_IP6 ? ((ip_event_got_ip6_t *)data)->esp_netif
                                                : ((ip_event_got_ip_t *)data)->esp_netif;
    if (id != IP_EVENT_GOT_IP6) {
        esp_netif_create_ip6_linklocal(netif);
    }
    if (s.started && strcmp(esp_netif_get_ifkey(netif), "WIFI_STA_DEF") == 0) {
        LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
            LogErrorOnFailure(chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(1),
                                                                          restart_mdns, nullptr));
        }));
    }
}

static void start(void)
{
    if (!s.on || s.started || s.failed) {
        return;
    }
    if (!s.loaded) {
        esp_err_t err = open_part();
        err = err == ESP_OK ? load() : err;
        if (err != ESP_OK) {
            const char *why[] = {esp_err_to_name(err), nullptr};
            db_log_event_args(TAG, DB_EV_MATTER_STORAGE, why);
            return;
        }
        s.loaded = true;
    }
    chip::Crypto::Spake2pVerifier v;
    chip::MutableByteSpan vs(s.verifier);
    if (v.Generate(ITERS, chip::ByteSpan(s.salt), s.pin) != CHIP_NO_ERROR ||
        v.Serialize(vs) != CHIP_NO_ERROR) {
        db_log_event_args(TAG, DB_EV_MATTER_NO_CODE, nullptr);
        return;
    }
    set_custom_commissionable_data_provider(&commissionable);
    chip::DeviceLayer::SetDeviceInfoProvider(&labels);

    node::config_t cfg;
    strlcpy(cfg.root_node.basic_information.node_label, db_config()->name,
            sizeof(cfg.root_node.basic_information.node_label));
    s.failed = true;
    node_t *n = node::create(&cfg, nullptr, nullptr);
    endpoint::aggregator::config_t agg;
    s.aggregator = n ? endpoint::aggregator::create(n, &agg, ENDPOINT_FLAG_NONE, nullptr) : nullptr;
    s.q = xQueueCreate(64, sizeof(msg_t));
    if (!s.aggregator || !s.q) {
        db_log_event_args(TAG, DB_EV_MATTER_NO_MEMORY, nullptr);
        return;
    }
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, on_ip, nullptr);
    esp_err_t err = esp_matter::start(on_event);
    if (err != ESP_OK) {
        const char *why[] = {esp_err_to_name(err), nullptr};
        db_log_event_args(TAG, DB_EV_MATTER_FAILED, why);
        return;
    }
    {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        refresh();
    }
    s.failed = false;
    s.started = true;
    /* after started: their GOT_IP6 must reach on_ip, also when Matter is turned on with the network up */
    for (const char *key : {"ETH_DEF", "WIFI_STA_DEF"}) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
        if (netif && esp_netif_is_netif_up(netif)) {
            esp_netif_create_ip6_linklocal(netif);
        }
    }
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

bool db_matter_codes(db_json_t *j)
{
    if (!s.started) {
        return false;
    }
    char qr[32], code[16];
    payload(qr, sizeof(qr), code, sizeof(code));
    db_json_str(j, "qr", qr);
    db_json_str(j, "code", code);
    uint8_t bits[DB_UI_QR_BYTES];
    int n = db_ui_qr(qr, bits);
    char hex[4 + 2 * DB_UI_QR_BYTES + 1];
    char *o = hex + sprintf(hex, "%d:", n);
    for (int i = 0; i < (n * n + 7) / 8; i++) {
        o += sprintf(o, "%02x", bits[i]);
    }
    db_json_str(j, "qr_bits", hex);
    return true;
}

int db_matter_set_on(bool on)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(DB_MATTER_NVS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "on", on);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        return err;
    }
    s.on = on;
    if (on) {
        db_matter_start();
    }
    return on && !s.started ? ESP_FAIL : ESP_OK;
}

int db_matter_show(void)
{
    char qr[32], code[16];
    payload(qr, sizeof(qr), code, sizeof(code));
    return s.started ? db_ui_show_matter(qr, code) : 0;
}

void db_matter_start(void)
{
    if (!s.start_lock) {
        s.start_lock = xSemaphoreCreateMutex(); /* main calls first, before the web server runs */
        nvs_handle_t h;
        uint8_t on = 0;
        if (nvs_open(DB_MATTER_NVS, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, "on", &on);
            nvs_close(h);
        }
        s.on = on;
    }
    xSemaphoreTake(s.start_lock, portMAX_DELAY);
    start();
    xSemaphoreGive(s.start_lock);
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
    if (!s.loaded) { /* never on since the start: nothing has the area open */
        return db_ota_spare_erase();
    }
    if (s.started) {
        /* kept: the stack writes nothing more before the restart */
        chip::DeviceLayer::PlatformMgr().LockChipStack();
    }
    nvs_flash_deinit_partition(PART);
    esp_err_t err = nvs_flash_erase_partition(PART);
    if (err != ESP_OK && s.started) {
        nvs_flash_init_partition(PART);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    return err;
}
