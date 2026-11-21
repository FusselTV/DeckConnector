#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <vector>

#include "esp_event.h"
#include "esp_matter.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

using namespace esp_matter;

ESP_EVENT_DEFINE_BASE(IP_EVENT);

struct esp_matter::node_t {
};
struct esp_matter::cluster_t {
    endpoint_t *ep;
};
struct esp_matter::endpoint_t {
    uint16_t id;
    cluster_t info{this};
    uint16_t parent = 0;
    const char *type = "";
};

static node_t root;
static bool created;
static std::map<uint16_t, endpoint_t *> eps;
static SemaphoreHandle_t stack;
static chip::DeviceLayer::DeviceInfoProvider *info;
static std::vector<uint16_t> unread; /* enabled endpoints whose labels a controller has yet to read */

/* One line per thing a controller would see. Tests read it, in order. */
static void trace(const char *fmt, ...)
{
    static const char *path = getenv("DB_SIM_MATTER_LOG");
    static FILE *f = path ? fopen(path, "a") : nullptr;
    if (!f) {
        if (path) {
            abort(); /* a test reads it: no log would look like a stack that never started */
        }
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

static void str_attr(cluster_t *c, const char *name, const char *v, uint16_t n)
{
    trace("attr %u %s %.*s", c->ep->id, name, n, v);
}

esp_matter_attr_val_t esp_matter_bool(bool v)
{
    esp_matter_attr_val_t a{};
    a.type = esp_matter_attr_val_t::BOOL;
    a.val.b = v;
    return a;
}

esp_matter_attr_val_t esp_matter_uint8(uint8_t v)
{
    esp_matter_attr_val_t a{};
    a.type = esp_matter_attr_val_t::UINT8;
    a.val.u8 = v;
    return a;
}

esp_matter_attr_val_t esp_matter_char_str(char *s, uint16_t n)
{
    esp_matter_attr_val_t a{};
    a.type = esp_matter_attr_val_t::CHAR_STR;
    a.val.str.s = s;
    a.val.str.n = n;
    return a;
}

namespace chip {
app::DnssdServer &app::DnssdServer::Instance()
{
    static DnssdServer d;
    return d;
}
void app::DnssdServer::StartServer()
{
    trace("dnssd restart");
}
CHIP_ERROR System::Layer::StartTimer(Clock::Seconds16, void (*cb)(Layer *, void *), void *arg)
{
    cb(this, arg); /* at once: no test waits for the second */
    return CHIP_NO_ERROR;
}
CHIP_ERROR DeviceLayer::PlatformManager::ScheduleWork(void (*work)(intptr_t), intptr_t arg)
{
    work(arg);
    return CHIP_NO_ERROR;
}
void DeviceLayer::PlatformManager::LockChipStack()
{
    xSemaphoreTake(stack, portMAX_DELAY);
}
/* A controller reads an endpoint's FixedLabel list once the change that made it is complete */
static void read_labels(void)
{
    for (uint16_t id : unread) {
        auto *it = info && eps.count(id) ? info->IterateFixedLabel(id) : nullptr;
        if (!it) {
            continue;
        }
        DeviceLayer::DeviceInfoProvider::FixedLabelType l;
        while (it->Next(l)) {
            trace("label %u %.*s=%.*s", id, (int)l.label.size(), l.label.data(), (int)l.value.size(), l.value.data());
        }
        it->Release();
    }
    unread.clear();
}
void DeviceLayer::PlatformManager::UnlockChipStack()
{
    read_labels();
    xSemaphoreGive(stack);
}
DeviceLayer::PlatformManager &DeviceLayer::PlatformMgr()
{
    static PlatformManager m;
    return m;
}
System::Layer &DeviceLayer::SystemLayer()
{
    static System::Layer l;
    return l;
}
void DeviceLayer::SetDeviceInfoProvider(DeviceInfoProvider *p)
{
    info = p;
}
uint8_t FabricTable::FabricCount() const
{
    return 0;
}
bool CommissioningWindowManager::IsCommissioningWindowOpen() const
{
    return true; /* never paired: CHIP keeps the window open */
}
CHIP_ERROR CommissioningWindowManager::OpenBasicCommissioningWindow(System::Clock::Seconds32)
{
    return CHIP_NO_ERROR;
}
Server &Server::GetInstance()
{
    static Server s;
    return s;
}
FabricTable &Server::GetFabricTable()
{
    static FabricTable f;
    return f;
}
CommissioningWindowManager &Server::GetCommissioningWindowManager()
{
    static CommissioningWindowManager w;
    return w;
}
CHIP_ERROR Crypto::Spake2pVerifier::Generate(uint32_t, ByteSpan, uint32_t)
{
    return CHIP_NO_ERROR;
}
CHIP_ERROR Crypto::Spake2pVerifier::Serialize(MutableByteSpan &)
{
    return CHIP_NO_ERROR;
}
bool SetupPayload::IsValidSetupPIN(uint32_t pin)
{
    return pin > 0 && pin < 99999999 && pin != 11111111 && pin != 12345678;
}
/* Not CHIP's encodings: the simulator's codes only have to differ per board and pairing data */
CHIP_ERROR QRCodeBasicSetupPayloadGenerator::payloadBase38Representation(MutableCharSpan &out)
{
    out.reduce_size((size_t)snprintf(out.data(), out.size(), "MT:SIM%04X%08lu", p.discriminator.value,
                                     (unsigned long)p.setUpPINCode));
    return CHIP_NO_ERROR;
}
CHIP_ERROR ManualSetupPayloadGenerator::payloadDecimalStringRepresentation(MutableCharSpan &out)
{
    out.reduce_size((size_t)snprintf(out.data(), out.size(), "%011lu", (unsigned long)p.setUpPINCode));
    return CHIP_NO_ERROR;
}
} // namespace chip

esp_err_t esp_matter::start(event_callback_t)
{
    trace("start");
    return ESP_OK;
}

void esp_matter::set_custom_commissionable_data_provider(chip::DeviceLayer::CommissionableDataProvider *)
{
}

lock::ScopedChipStackLock::ScopedChipStackLock(uint64_t)
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
}
lock::ScopedChipStackLock::~ScopedChipStackLock()
{
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

node_t *node::create(config_t *, void *, void *)
{
    if (created) {
        abort(); /* the real one makes a second root node */
    }
    created = true;
    stack = xSemaphoreCreateMutex();
    return &root;
}
node_t *node::get()
{
    return created ? &root : nullptr;
}

cluster_t *cluster::get(endpoint_t *ep, uint32_t)
{
    return &ep->info;
}
cluster_t *cluster::descriptor::create(endpoint_t *ep, config_t *, uint8_t)
{
    return &ep->info;
}
cluster_t *cluster::fixed_label::create(endpoint_t *ep, config_t *, uint8_t)
{
    return &ep->info;
}
void cluster::bridged_device_basic_information::attribute::create_node_label(cluster_t *c, char *v, uint16_t n)
{
    str_attr(c, "node_label", v, n);
}
void cluster::bridged_device_basic_information::attribute::create_vendor_id(cluster_t *c, uint16_t v)
{
    trace("attr %u vendor_id %u", c->ep->id, v);
}
void cluster::bridged_device_basic_information::attribute::create_product_id(cluster_t *c, uint16_t v)
{
    trace("attr %u product_id %u", c->ep->id, v);
}
void cluster::bridged_device_basic_information::attribute::create_product_name(cluster_t *c, char *v, uint16_t n)
{
    str_attr(c, "product_name", v, n);
}
void cluster::bridged_device_basic_information::attribute::create_serial_number(cluster_t *c, char *v, uint16_t n)
{
    str_attr(c, "serial_number", v, n);
}

static esp_err_t event(uint16_t ep, const char *name)
{
    if (!eps.count(ep)) {
        abort(); /* the real stack drops it quietly; a test would never see the miss */
    }
    trace("event %u %s", ep, name);
    return ESP_OK;
}
esp_err_t cluster::switch_cluster::event::send_initial_press(uint16_t ep, uint8_t)
{
    return ::event(ep, "initial_press");
}
esp_err_t cluster::switch_cluster::event::send_long_press(uint16_t ep, uint8_t)
{
    return ::event(ep, "long_press");
}
esp_err_t cluster::switch_cluster::event::send_short_release(uint16_t ep, uint8_t)
{
    return ::event(ep, "short_release");
}
esp_err_t cluster::switch_cluster::event::send_long_release(uint16_t ep, uint8_t)
{
    return ::event(ep, "long_release");
}

esp_err_t attribute::update(uint16_t ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t *v)
{
    using namespace chip::app::Clusters;
    namespace info = BridgedDeviceBasicInformation;
    if (!eps.count(ep)) {
        abort();
    }
    if (cluster == info::Id && attr == info::Attributes::Reachable::Id && v->type == v->BOOL) {
        trace("attr %u reachable %d", ep, v->val.b);
    } else if (cluster == info::Id && attr == info::Attributes::NodeLabel::Id && v->type == v->CHAR_STR) {
        trace("attr %u node_label %.*s", ep, v->val.str.n, v->val.str.s);
    } else if (cluster == Switch::Id && attr == Switch::Attributes::CurrentPosition::Id && v->type == v->UINT8) {
        trace("attr %u position %u", ep, v->val.u8);
    } else {
        abort(); /* an attribute this fake does not know: teach it, or db_matter sends a wrong one */
    }
    return ESP_OK;
}

endpoint_t *endpoint::resume(node_t *, uint8_t, uint16_t id, void *)
{
    if (eps.count(id)) {
        return nullptr;
    }
    endpoint_t *ep = new endpoint_t{id};
    eps[id] = ep;
    return ep;
}
endpoint_t *endpoint::get(node_t *, uint16_t id)
{
    auto it = eps.find(id);
    return it == eps.end() ? nullptr : it->second;
}
esp_err_t endpoint::destroy(node_t *, endpoint_t *ep)
{
    trace("gone %u", ep->id);
    eps.erase(ep->id);
    delete ep;
    return ESP_OK;
}
esp_err_t endpoint::set_parent_endpoint(endpoint_t *ep, endpoint_t *parent)
{
    ep->parent = parent->id;
    return ESP_OK;
}
esp_err_t endpoint::set_semantic_tags(endpoint_t *ep, chip::app::DataModel::Provider::SemanticTag *tags, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const chip::CharSpan &l = tags[i].label.value.value;
        trace("tag %u %u %u %.*s", ep->id, tags[i].namespaceID, tags[i].tag, (int)l.size(), l.data());
    }
    return ESP_OK;
}
esp_err_t endpoint::enable(endpoint_t *ep)
{
    trace("endpoint %u %s parent %u", ep->id, ep->type, ep->parent);
    unread.push_back(ep->id);
    return ESP_OK;
}
endpoint_t *endpoint::aggregator::create(node_t *n, config_t *, uint8_t flags, void *)
{
    endpoint_t *ep = resume(n, flags, 1, nullptr);
    ep->type = "aggregator";
    enable(ep);
    return ep;
}
esp_err_t endpoint::bridged_node::add(endpoint_t *ep, config_t *cfg)
{
    ep->type = "bridged_node";
    trace("attr %u unique_id %s", ep->id, cfg->bridged_device_basic_information.unique_id);
    trace("attr %u reachable %d", ep->id, cfg->bridged_device_basic_information.reachable);
    return ESP_OK;
}
esp_err_t endpoint::generic_switch::add(endpoint_t *ep, config_t *cfg)
{
    ep->type = "generic_switch";
    trace("attr %u features %lu", ep->id, (unsigned long)cfg->switch_cluster.feature_flags);
    trace("attr %u identify_type %u", ep->id, cfg->identify.identify_type);
    return ESP_OK;
}

/* The board's interfaces get IPv6 here; the simulator's take none */
esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *)
{
    return ESP_OK;
}
bool esp_netif_is_netif_up(esp_netif_t *)
{
    return false;
}
