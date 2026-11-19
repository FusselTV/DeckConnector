/* esp-matter and CHIP as far as components/db_matter uses them, without a stack: the data model is a list of
 * endpoints, and what a controller would see goes to DB_SIM_MATTER_LOG, one line each (fake.cpp). */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <initializer_list>
#include <type_traits>

#include "esp_err.h"
#include "esp_event.h"

#define CONFIG_DEVICE_VENDOR_ID  0xFFF1
#define CONFIG_DEVICE_PRODUCT_ID 0x8000

typedef int CHIP_ERROR;
#define CHIP_NO_ERROR              0
#define CHIP_ERROR_NOT_IMPLEMENTED 0x2d
#define LogErrorOnFailure(e)       ((void)(e))

namespace chip {

using EndpointId = uint16_t;

template <typename T> class Span
{
  public:
    Span() = default;
    Span(T *p, size_t n) : data_(p), size_(n) {}
    template <size_t N> Span(T (&a)[N]) : data_(a), size_(N) {}
    static Span fromCharString(T *s)
    {
        return Span(s, strlen(s));
    }
    T *data() const
    {
        return data_;
    }
    size_t size() const
    {
        return size_;
    }
    void reduce_size(size_t n)
    {
        size_ = n;
    }

  private:
    T *data_ = nullptr;
    size_t size_ = 0;
};
using ByteSpan = Span<const uint8_t>;
using MutableByteSpan = Span<uint8_t>;
using CharSpan = Span<const char>;
using MutableCharSpan = Span<char>;

inline CHIP_ERROR CopySpanToMutableSpan(ByteSpan from, MutableByteSpan &to)
{
    if (from.size() > to.size()) {
        return 0x19; /* CHIP_ERROR_BUFFER_TOO_SMALL */
    }
    memcpy(to.data(), from.data(), from.size());
    to.reduce_size(from.size());
    return CHIP_NO_ERROR;
}

template <typename E> constexpr auto to_underlying(E e)
{
    return static_cast<std::underlying_type_t<E>>(e);
}

template <typename T> struct Optional {
    bool has = false;
    T value{};
    void SetValue(const T &v)
    {
        has = true;
        value = v;
    }
};

namespace app {
namespace DataModel {
template <typename T> struct Nullable {
    T value{};
};
template <typename T> Nullable<T> MakeNullable(T v)
{
    return Nullable<T>{v};
}
struct Provider {
    struct SemanticTag {
        uint8_t namespaceID = 0;
        uint8_t tag = 0;
        Optional<Nullable<CharSpan>> label;
    };
};
} // namespace DataModel

namespace Clusters {
namespace Identify {
enum class IdentifyTypeEnum : uint8_t { kNone = 0 };
}
namespace Switch {
constexpr uint32_t Id = 0x3B;
namespace Attributes::CurrentPosition {
constexpr uint32_t Id = 0x1;
}
} // namespace Switch
namespace BridgedDeviceBasicInformation {
constexpr uint32_t Id = 0x39;
namespace Attributes::NodeLabel {
constexpr uint32_t Id = 0x5;
}
namespace Attributes::Reachable {
constexpr uint32_t Id = 0x11;
}
} // namespace BridgedDeviceBasicInformation
} // namespace Clusters

class DnssdServer
{
  public:
    static DnssdServer &Instance();
    void StartServer();
};
} // namespace app

namespace System {
namespace Clock {
struct Seconds16 {
    explicit Seconds16(uint16_t s) : s(s) {}
    uint16_t s;
};
struct Seconds32 {
    explicit Seconds32(uint32_t s) : s(s) {}
    uint32_t s;
};
} // namespace Clock
class Layer
{
  public:
    CHIP_ERROR StartTimer(Clock::Seconds16 delay, void (*cb)(Layer *, void *), void *arg);
};
} // namespace System

namespace DeviceLayer {
namespace DeviceEventType {
enum : uint16_t { kCommissioningComplete = 0x8000, kFabricRemoved };
}
struct ChipDeviceEvent {
    uint16_t Type;
};

class PlatformManager
{
  public:
    CHIP_ERROR ScheduleWork(void (*work)(intptr_t), intptr_t arg = 0);
    void LockChipStack();
    void UnlockChipStack();
};
PlatformManager &PlatformMgr();
System::Layer &SystemLayer();

class CommissionableDataProvider
{
  public:
    virtual ~CommissionableDataProvider() = default;
    virtual CHIP_ERROR GetSetupDiscriminator(uint16_t &d) = 0;
    virtual CHIP_ERROR SetSetupDiscriminator(uint16_t d) = 0;
    virtual CHIP_ERROR GetSpake2pIterationCount(uint32_t &n) = 0;
    virtual CHIP_ERROR GetSpake2pSalt(MutableByteSpan &out) = 0;
    virtual CHIP_ERROR GetSpake2pVerifier(MutableByteSpan &out, size_t &len) = 0;
    virtual CHIP_ERROR GetSetupPasscode(uint32_t &p) = 0;
    virtual CHIP_ERROR SetSetupPasscode(uint32_t p) = 0;
};

class DeviceInfoProvider
{
  public:
    template <typename T> class Iterator
    {
      public:
        virtual ~Iterator() = default;
        virtual size_t Count() = 0;
        virtual bool Next(T &item) = 0;
        virtual void Release() = 0;
    };
    struct FixedLabelType {
        CharSpan label, value;
    };
    using UserLabelType = FixedLabelType;
    using FixedLabelIterator = Iterator<FixedLabelType>;
    using UserLabelIterator = Iterator<UserLabelType>;
    using SupportedLocalesIterator = Iterator<CharSpan>;
    using SupportedCalendarTypesIterator = Iterator<uint8_t>;

    virtual ~DeviceInfoProvider() = default;
    virtual FixedLabelIterator *IterateFixedLabel(EndpointId ep) = 0;
    virtual UserLabelIterator *IterateUserLabel(EndpointId ep) = 0;
    virtual SupportedLocalesIterator *IterateSupportedLocales() = 0;
    virtual SupportedCalendarTypesIterator *IterateSupportedCalendarTypes() = 0;
    virtual CHIP_ERROR SetUserLabelAt(EndpointId ep, size_t i, const UserLabelType &l) = 0;
    virtual CHIP_ERROR DeleteUserLabelAt(EndpointId ep, size_t i) = 0;
    virtual CHIP_ERROR SetUserLabelLength(EndpointId ep, size_t n) = 0;
    virtual CHIP_ERROR GetUserLabelLength(EndpointId ep, size_t &n) = 0;
};
void SetDeviceInfoProvider(DeviceInfoProvider *p);
} // namespace DeviceLayer

class FabricTable
{
  public:
    uint8_t FabricCount() const;
};
class CommissioningWindowManager
{
  public:
    bool IsCommissioningWindowOpen() const;
    CHIP_ERROR OpenBasicCommissioningWindow(System::Clock::Seconds32 t);
};
class Server
{
  public:
    static Server &GetInstance();
    FabricTable &GetFabricTable();
    CommissioningWindowManager &GetCommissioningWindowManager();
};

namespace Crypto {
constexpr size_t kSpake2p_VerifierSerialized_Length = 97;
class Spake2pVerifier
{
  public:
    CHIP_ERROR Generate(uint32_t iters, ByteSpan salt, uint32_t pin);
    CHIP_ERROR Serialize(MutableByteSpan &out);
};
} // namespace Crypto

enum class RendezvousInformationFlag : uint8_t { kOnNetwork = 4 };
struct PayloadContents {
    uint16_t vendorID = 0, productID = 0;
    Optional<RendezvousInformationFlag> rendezvousInformation;
    struct {
        uint16_t value = 0;
        void SetLongValue(uint16_t v)
        {
            value = v;
        }
    } discriminator;
    uint32_t setUpPINCode = 0;
};
struct SetupPayload {
    static bool IsValidSetupPIN(uint32_t pin);
};
class QRCodeBasicSetupPayloadGenerator
{
  public:
    explicit QRCodeBasicSetupPayloadGenerator(const PayloadContents &p) : p(p) {}
    CHIP_ERROR payloadBase38Representation(MutableCharSpan &out);

  private:
    PayloadContents p;
};
class ManualSetupPayloadGenerator
{
  public:
    explicit ManualSetupPayloadGenerator(const PayloadContents &p) : p(p) {}
    CHIP_ERROR payloadDecimalStringRepresentation(MutableCharSpan &out);

  private:
    PayloadContents p;
};
} // namespace chip

using chip::DeviceLayer::ChipDeviceEvent;

typedef struct {
    enum { BOOL, UINT8, CHAR_STR } type;
    union {
        bool b;
        uint8_t u8;
        struct {
            const char *s;
            uint16_t n;
        } str;
    } val;
} esp_matter_attr_val_t;
esp_matter_attr_val_t esp_matter_bool(bool v);
esp_matter_attr_val_t esp_matter_uint8(uint8_t v);
esp_matter_attr_val_t esp_matter_char_str(char *s, uint16_t n);

namespace esp_matter {
struct node_t;
struct endpoint_t;
struct cluster_t;

enum : uint8_t { ENDPOINT_FLAG_NONE = 0, ENDPOINT_FLAG_DESTROYABLE = 1, ENDPOINT_FLAG_BRIDGE = 2 };
enum : uint8_t { CLUSTER_FLAG_SERVER = 2 };

typedef void (*event_callback_t)(const ChipDeviceEvent *event, intptr_t arg);
esp_err_t start(event_callback_t cb);
void set_custom_commissionable_data_provider(chip::DeviceLayer::CommissionableDataProvider *p);

namespace lock {
class ScopedChipStackLock
{
  public:
    explicit ScopedChipStackLock(uint64_t ticks);
    ~ScopedChipStackLock();
};
} // namespace lock

namespace node {
struct config_t {
    struct {
        struct {
            char node_label[33] = "";
        } basic_information;
    } root_node;
};
node_t *create(config_t *cfg, void *attr_cb, void *identify_cb);
node_t *get();
} // namespace node

namespace cluster {
cluster_t *get(endpoint_t *ep, uint32_t id);
namespace descriptor {
struct config_t {};
cluster_t *create(endpoint_t *ep, config_t *cfg, uint8_t flags);
} // namespace descriptor
namespace fixed_label {
struct config_t {};
cluster_t *create(endpoint_t *ep, config_t *cfg, uint8_t flags);
} // namespace fixed_label
namespace bridged_device_basic_information::attribute {
void create_node_label(cluster_t *c, char *v, uint16_t n);
void create_vendor_id(cluster_t *c, uint16_t v);
void create_product_id(cluster_t *c, uint16_t v);
void create_product_name(cluster_t *c, char *v, uint16_t n);
void create_serial_number(cluster_t *c, char *v, uint16_t n);
} // namespace bridged_device_basic_information::attribute
namespace switch_cluster {
namespace feature {
namespace momentary_switch {
constexpr uint32_t get_id()
{
    return 0x2;
}
} // namespace momentary_switch
namespace momentary_switch_release {
constexpr uint32_t get_id()
{
    return 0x4;
}
} // namespace momentary_switch_release
namespace momentary_switch_long_press {
constexpr uint32_t get_id()
{
    return 0x8;
}
} // namespace momentary_switch_long_press
} // namespace feature
namespace event {
esp_err_t send_initial_press(uint16_t ep, uint8_t pos);
esp_err_t send_long_press(uint16_t ep, uint8_t pos);
esp_err_t send_short_release(uint16_t ep, uint8_t pos);
esp_err_t send_long_release(uint16_t ep, uint8_t pos);
} // namespace event
} // namespace switch_cluster
} // namespace cluster

namespace attribute {
esp_err_t update(uint16_t ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t *v);
}

namespace endpoint {
endpoint_t *resume(node_t *n, uint8_t flags, uint16_t id, void *priv);
endpoint_t *get(node_t *n, uint16_t id);
esp_err_t destroy(node_t *n, endpoint_t *ep);
esp_err_t enable(endpoint_t *ep);
esp_err_t set_parent_endpoint(endpoint_t *ep, endpoint_t *parent);
esp_err_t set_semantic_tags(endpoint_t *ep, chip::app::DataModel::Provider::SemanticTag *tags, size_t n);
namespace aggregator {
struct config_t {};
endpoint_t *create(node_t *n, config_t *cfg, uint8_t flags, void *priv);
} // namespace aggregator
namespace bridged_node {
struct config_t {
    cluster::descriptor::config_t descriptor;
    struct {
        char unique_id[33] = "";
        bool reachable = false;
    } bridged_device_basic_information;
};
esp_err_t add(endpoint_t *ep, config_t *cfg);
} // namespace bridged_node
namespace generic_switch {
struct config_t {
    cluster::descriptor::config_t descriptor;
    struct {
        uint8_t identify_type = 0;
    } identify;
    struct {
        uint32_t feature_flags = 0;
    } switch_cluster;
};
esp_err_t add(endpoint_t *ep, config_t *cfg);
} // namespace generic_switch
} // namespace endpoint
} // namespace esp_matter
