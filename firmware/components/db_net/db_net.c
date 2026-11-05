#include "db_net.h"

#include <stdio.h>
#include <string.h>

#include "db_board.h"
#include "db_config.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"

#define LAN_ROUTE_PRIO 200 /* above the WLAN station (100): LAN wins when both are up */

static const char *TAG = "net";

static struct {
    SemaphoreHandle_t lock;
    esp_netif_t *lan;
    esp_netif_t *wlan;
    esp_netif_t *ap;
    db_net_status_t st;
    bool wifi_started;
    void (*on_change)(void);
} s;

/* Hosts keep the first mDNS answer per name, so with both up in one subnet they could pick WLAN. */
static void mdns_follow(void)
{
    if (s.wlan) {
        bool announce = s.st.wlan_up && !s.st.lan_up;
        mdns_netif_action(s.wlan, announce ? MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4
                                           : MDNS_EVENT_DISABLE_IP4);
    }
}

static void changed(void)
{
    mdns_follow();
    if (s.on_change) {
        s.on_change();
    }
}

static void set_ip(char *dst, const esp_ip4_addr_t *ip)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (ip) {
        snprintf(dst, 16, IPSTR, IP2STR(ip));
    } else {
        dst[0] = '\0';
    }
    xSemaphoreGive(s.lock);
}

static void apply_static(esp_netif_t *netif, const db_ip_config_t *c)
{
    if (c->dhcp) {
        return;
    }
    esp_netif_ip_info_t info = {0};
    ip4addr_aton(c->ip, (ip4_addr_t *)&info.ip);
    ip4addr_aton(c->mask, (ip4_addr_t *)&info.netmask);
    ip4addr_aton(c->gateway, (ip4_addr_t *)&info.gw);
    esp_netif_dhcpc_stop(netif);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(netif, &info));
    if (c->dns[0]) {
        esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
        ip4addr_aton(c->dns, (ip4_addr_t *)&dns.ip.u_addr.ip4);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    ESP_LOGI(TAG, "%s: fixed address %s", esp_netif_get_desc(netif), c->ip);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = data;
    bool lan = e->esp_netif == s.lan;
    ESP_LOGI(TAG, "%s address " IPSTR, lan ? "LAN" : "WLAN", IP2STR(&e->ip_info.ip));
    set_ip(lan ? s.st.lan_ip : s.st.wlan_ip, &e->ip_info.ip);
    if (lan) {
        s.st.lan_up = true;
    } else {
        s.st.wlan_up = true;
    }
    changed();
}

static void on_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "LAN link up");
        s.st.lan_link = true;
        apply_static(s.lan, &db_config()->lan);
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "LAN link down");
        s.st.lan_link = false;
        s.st.lan_up = false;
        set_ip(s.st.lan_ip, NULL);
    }
    changed();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        if (db_config()->wifi_ssid[0]) {
            esp_wifi_connect();
        }
        break;
    case WIFI_EVENT_STA_CONNECTED:
        apply_static(s.wlan, &db_config()->wlan);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "WLAN lost, reason %d", d->reason);
        s.st.wlan_up = false;
        set_ip(s.st.wlan_ip, NULL);
        changed();
        if (db_config()->wifi_ssid[0]) {
            esp_wifi_connect();
        }
        break;
    }
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "setup AP: client joined");
        break;
    default:
        break;
    }
}

static esp_err_t start_eth(const db_board_t *b)
{
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.route_prio = LAN_ROUTE_PRIO;
    esp_netif_config_t nc = {.base = &base, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    s.lan = esp_netif_new(&nc);
    esp_netif_set_hostname(s.lan, db_config()->name);
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = b->eth.phy_addr;
    phy_cfg.reset_gpio_num = b->eth.phy_reset_gpio;
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = b->eth.mdc_gpio;
    emac.smi_gpio.mdio_num = b->eth.mdio_gpio;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&cfg, &eth), TAG, "eth driver");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s.lan, esp_eth_new_netif_glue(eth)), TAG, "eth netif");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, NULL), TAG, "eth ev");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, NULL), TAG, "ip ev");
    return esp_eth_start(eth);
}

static esp_err_t start_wifi(void)
{
    const db_config_t *c = db_config();
    s.wlan = esp_netif_create_default_wifi_sta();
    char host[40];
    snprintf(host, sizeof(host), "%s-wlan", c->name);
    esp_netif_set_hostname(s.wlan, host);
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wc), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL), TAG,
                        "wifi ev");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL), TAG, "ip ev");
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, c->wifi_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, c->wifi_pass, sizeof(cfg.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    esp_wifi_set_ps(WIFI_PS_NONE); /* power save adds up to 100 ms of latency */
    s.wifi_started = true;
    s.st.wlan_chip = true;
    return ESP_OK;
}

static void start_mdns(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS failed");
        return;
    }
    mdns_hostname_set(db_config()->name);
    mdns_instance_name_set(db_config()->name);
    if (s.wlan) {
        mdns_register_netif(s.wlan);
        mdns_follow();
    }
}

esp_err_t db_net_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    const db_board_t *b = db_board();
    if (b->eth.present) {
        esp_err_t err = start_eth(b);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LAN not available: %s", esp_err_to_name(err));
        }
    }
    if (b->wifi) {
        esp_err_t err = start_wifi();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WLAN not available: %s", esp_err_to_name(err));
        }
    }
    start_mdns();
    return ESP_OK;
}

void db_net_status(db_net_status_t *out)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    *out = s.st;
    xSemaphoreGive(s.lock);
    wifi_ap_record_t ap;
    out->rssi = (out->wlan_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}

bool db_net_ifname(bool wlan, char name[6])
{
    bool up = wlan ? s.st.wlan_up : s.st.lan_up;
    return up && esp_netif_get_netif_impl_name(wlan ? s.wlan : s.lan, name) == ESP_OK;
}

bool db_net_online(void)
{
    return s.st.lan_up || s.st.wlan_up;
}

void db_net_on_change(void (*cb)(void))
{
    s.on_change = cb;
}

esp_err_t db_net_ap_start(void)
{
    if (!s.wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s.ap) {
        s.ap = esp_netif_create_default_wifi_ap();
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    strlcpy(s.st.ap_ssid, db_config()->name, sizeof(s.st.ap_ssid));
    db_config_random_password(s.st.ap_pass, 9);
    xSemaphoreGive(s.lock);
    wifi_config_t cfg = {.ap = {.channel = 6, .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK}};
    strlcpy((char *)cfg.ap.ssid, s.st.ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(s.st.ap_ssid);
    strlcpy((char *)cfg.ap.password, s.st.ap_pass, sizeof(cfg.ap.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "apsta");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "ap config");
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(s.ap, &ip);
    set_ip(s.st.ap_ip, &ip.ip);
    s.st.ap_active = true;
    ESP_LOGI(TAG, "setup AP %s on %s", s.st.ap_ssid, s.st.ap_ip);
    changed();
    return ESP_OK;
}

esp_err_t db_net_ap_stop(void)
{
    if (!s.st.ap_active) {
        return ESP_OK;
    }
    s.st.ap_active = false;
    changed();
    return esp_wifi_set_mode(WIFI_MODE_STA);
}
