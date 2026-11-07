#include "db_net.h"
#include "db_log.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_board.h"
#include "db_config.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "ping/ping_sock.h"

#define RSSI_MAX_AGE_US 2000000
#define DHCP_KICK_US    12000000
#define LAN_ROUTE_PRIO  200 /* above the WLAN station (100): LAN wins when both are up */
#define FOUND_MAX       4
#define BROWSE_EVERY_US 5000000
#define LAN_DEAD_AFTER  3 /* gateway pings in a row without an answer */

static const char *TAG = "net";

static struct {
    SemaphoreHandle_t lock;
    esp_netif_t *lan;
    esp_netif_t *wlan;
    esp_netif_t *ap;
    db_net_status_t st;
    int64_t rssi_at;
    int64_t link_at;
    esp_timer_handle_t kick;
    bool wifi_started;
    void (*on_change)(void);
    db_net_found_t found[FOUND_MAX];
    size_t found_count;
    int64_t browsed_at;
    bool browsing;
    esp_ping_handle_t ping;
    int missed;
    bool gateway_seen;
    int join_reason;
} s;

/* A LAN can keep its link and drop every frame. With WLAN up, the dead LAN is not worth using. */
static bool lan_usable(void)
{
    return s.st.lan_up && (!s.st.lan_dead || !s.st.wlan_up);
}

/* One interface announces at a time: hosts keep the first answer per name, and esp mdns takes our own
 * announcement heard on the other interface for a rival and renames the service. */
static void mdns_follow(void)
{
    static int last = -1;
    bool lan = lan_usable();
    bool wlan = s.wlan && s.st.wlan_up && !lan;
    if ((lan | wlan << 1) == last) {
        return;
    }
    if (s.wlan && !wlan) {
        mdns_netif_action(s.wlan, MDNS_EVENT_DISABLE_IP4);
    }
    if (s.st.lan_up) {
        mdns_netif_action(s.lan, lan ? MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4 : MDNS_EVENT_DISABLE_IP4);
    }
    if (wlan) {
        mdns_register_netif(s.wlan); /* mdns forgets a netif of ours when it disables it */
        mdns_netif_action(s.wlan, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
    }
    last = lan | wlan << 1;
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

static void note_gateway(bool answered)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool was = s.st.lan_dead;
    if (answered) {
        s.gateway_seen = true;
        s.missed = 0;
        s.st.lan_dead = false;
    } else if (s.gateway_seen && ++s.missed >= LAN_DEAD_AFTER) { /* a gateway that never answers pings proves nothing */
        s.st.lan_dead = true;
    }
    bool dead = s.st.lan_dead;
    xSemaphoreGive(s.lock);
    if (dead == was) {
        return;
    }
    db_log_event(TAG, "lan", "%s", dead ? "LAN dead: the gateway stopped answering" : "LAN answers again");
    changed();
}

static void ping_ok(esp_ping_handle_t h, void *arg)
{
    note_gateway(true);
}

static void ping_lost(esp_ping_handle_t h, void *arg)
{
    note_gateway(false);
}

static void watch_gateway(const esp_netif_ip_info_t *ip)
{
    if (s.ping) {
        esp_ping_stop(s.ping);
        esp_ping_delete_session(s.ping);
        s.ping = NULL;
    }
    s.missed = 0;
    s.gateway_seen = false;
    s.st.lan_dead = false;
    esp_ping_config_t c = ESP_PING_DEFAULT_CONFIG();
    c.target_addr.type = IPADDR_TYPE_V4;
    c.target_addr.u_addr.ip4.addr = ip->gw.addr;
    c.count = ESP_PING_COUNT_INFINITE;
    c.interval_ms = 1000;
    c.timeout_ms = 1000;
    c.interface = esp_netif_get_netif_impl_index(s.lan);
    esp_ping_callbacks_t cb = {.on_ping_success = ping_ok, .on_ping_timeout = ping_lost};
    if (ip->gw.addr && esp_ping_new_session(&c, &cb, &s.ping) == ESP_OK) {
        esp_ping_start(s.ping);
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = data;
    bool lan = e->esp_netif == s.lan;
    ESP_LOGI(TAG, "%s address " IPSTR, lan ? "LAN" : "WLAN", IP2STR(&e->ip_info.ip));
    set_ip(lan ? s.st.lan_ip : s.st.wlan_ip, &e->ip_info.ip);
    db_log_mark(lan ? DB_MARK_LAN_IP : DB_MARK_WLAN_IP);
    if (lan) {
        s.st.lan_up = true;
        watch_gateway(&e->ip_info);
    } else {
        s.st.wlan_up = true;
    }
    changed();
}

/* A switch port with spanning tree drops the first seconds of frames after the link comes up, and DHCP
 * retries after 1, 2 and 4 s: the address came up to 6 s late. Asking again every second gets it a second
 * after the port forwards. */
static void dhcp_kick(void *arg)
{
    int64_t since = esp_timer_get_time() - s.link_at;
    if (s.st.lan_up || !s.st.lan_link || since > DHCP_KICK_US) {
        esp_timer_stop(s.kick);
        return;
    }
    esp_netif_dhcpc_stop(s.lan);
    esp_netif_dhcpc_start(s.lan);
}

static void on_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "LAN link up");
        db_log_mark(DB_MARK_LAN_LINK);
        s.st.lan_link = true;
        apply_static(s.lan, &db_config()->lan);
        if (db_config()->lan.dhcp && s.kick) {
            s.link_at = esp_timer_get_time();
            esp_timer_stop(s.kick);
            esp_timer_start_periodic(s.kick, 1000000);
        }
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        db_log_event(TAG, "lan", "LAN link down");
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
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *c = data;
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", c->bssid[0], c->bssid[1], c->bssid[2],
                 c->bssid[3], c->bssid[4], c->bssid[5]);
        if (s.st.wlan_bssid[0] && strcmp(s.st.wlan_bssid, bssid) != 0) {
            db_log_event(TAG, "wlan", "WLAN roam %.*s %s -> %s, channel %u", c->ssid_len, c->ssid, s.st.wlan_bssid,
                         bssid, c->channel);
        } else {
            db_log_event(TAG, "wlan", "WLAN joined %.*s %s, channel %u", c->ssid_len, c->ssid, bssid, c->channel);
        }
        snprintf(s.st.wlan_ssid, sizeof(s.st.wlan_ssid), "%.*s", c->ssid_len, c->ssid);
        strcpy(s.st.wlan_bssid, bssid);
        s.st.wlan_channel = c->channel;
        apply_static(s.wlan, &db_config()->wlan);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "WLAN lost, reason %d", d->reason);
        s.join_reason = d->reason;
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
    esp_timer_create_args_t kick = {.callback = dhcp_kick, .name = "dhcp_kick"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&kick, &s.kick), TAG, "dhcp timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, NULL), TAG, "eth ev");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, NULL), TAG, "ip ev");
    return esp_eth_start(eth);
}

static wifi_config_t sta_config(const char *ssid, const char *pass, const char *bssid)
{
    wifi_config_t cfg = {0};
    /* both fields take their full length without a NUL: a 32-byte SSID, a 64-hex-digit key */
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    cfg.sta.bssid_set = db_config_bssid(bssid, cfg.sta.bssid);
    return cfg;
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
    wifi_config_t cfg = sta_config(c->wifi_ssid, c->wifi_pass, c->wifi_bssid);
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

/* Answer every A query with the address of the setup AP while it is on. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "captive DNS not available");
        vTaskDelete(NULL);
    }
    uint8_t pkt[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, pkt, sizeof(pkt) - 16, 0, (struct sockaddr *)&from, &from_len);
        db_net_status_t st;
        db_net_status(&st);
        if (n < 12 || !st.ap_active) {
            continue;
        }
        /* Keep only header and question: an EDNS record after the question would hide our answer. */
        int q = 12;
        while (q < n && pkt[q] != 0) {
            q += pkt[q] + 1;
        }
        q += 5; /* zero label, type, class */
        if (q > n) {
            continue;
        }
        n = q;
        memset(pkt + 8, 0, 4); /* no authority or additional records */
        struct in_addr ip;
        inet_aton(st.ap_ip, &ip);
        pkt[2] = 0x81; /* response, recursion desired */
        pkt[3] = 0x80; /* recursion available, no error */
        pkt[7] = 1;    /* one answer */
        const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
        memcpy(pkt + n, answer, sizeof(answer));
        memcpy(pkt + n + sizeof(answer), &ip.s_addr, 4);
        sendto(sock, pkt, (size_t)n + sizeof(answer) + 4, 0, (struct sockaddr *)&from, from_len);
    }
}

static void on_time(struct timeval *tv)
{
    char at[32];
    strftime(at, sizeof(at), "%Y-%m-%d %H:%M:%S UTC", gmtime(&tv->tv_sec));
    ESP_LOGI(TAG, "clock set: %s", at);
}

static void start_sntp(void)
{
    const char *own = db_config()->ntp_server;
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(own[0] ? own : "pool.ntp.org");
    sc.server_from_dhcp = !own[0];
    sc.index_of_first_server = own[0] ? 0 : 1; /* the DHCP server's NTP server takes slot 0 */
    sc.wait_for_sync = false;
    sc.sync_cb = on_time;
    esp_netif_sntp_init(&sc);
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
    start_sntp();
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    return ESP_OK;
}

void db_net_status(db_net_status_t *out)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    /* the signal is a call to the WLAN chip over SDIO, too slow for every status and every redraw */
    int64_t now = esp_timer_get_time();
    if (s.st.wlan_up && now - s.rssi_at > RSSI_MAX_AGE_US) {
        wifi_ap_record_t ap;
        s.st.rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
        s.rssi_at = now;
    }
    *out = s.st;
    xSemaphoreGive(s.lock);
    out->rssi = out->wlan_up ? out->rssi : 0;
}

bool db_net_ifname(bool wlan, char name[6])
{
    bool up = wlan ? s.st.wlan_up : lan_usable();
    return up && esp_netif_get_netif_impl_name(wlan ? s.wlan : s.lan, name) == ESP_OK;
}

bool db_net_pick(bool *wlan, char name[6])
{
    if (db_net_ifname(*wlan, name)) {
        return true;
    }
    *wlan = !*wlan;
    return db_net_ifname(*wlan, name);
}

bool db_net_has_ip(uint32_t ip)
{
    db_net_status_t n;
    db_net_status(&n);
    return (n.lan_up && inet_addr(n.lan_ip) == ip) || (n.wlan_up && inet_addr(n.wlan_ip) == ip);
}

int db_net_syn_from(uint32_t local_ip, uint16_t local_port, uint32_t ip, uint16_t port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    fcntl(sock, F_SETFL, O_NONBLOCK);
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = local_port, .sin_addr.s_addr = local_ip};
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = port, .sin_addr.s_addr = ip};
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        close(sock);
        return -1;
    }
    connect(sock, (struct sockaddr *)&peer, sizeof(peer));
    return sock;
}

bool db_net_online(void)
{
    return s.st.lan_up || s.st.wlan_up;
}

void db_net_on_change(void (*cb)(void))
{
    s.on_change = cb;
}

static bool in_our_subnet(const esp_ip4_addr_t *a)
{
    esp_netif_t *nets[] = {s.lan, s.wlan};
    for (int i = 0; i < 2; i++) {
        esp_netif_ip_info_t ip;
        if (nets[i] && esp_netif_get_ip_info(nets[i], &ip) == ESP_OK && ip.ip.addr &&
            (a->addr & ip.netmask.addr) == (ip.ip.addr & ip.netmask.addr)) {
            return true;
        }
    }
    return false;
}

static void browse_task(void *arg)
{
    mdns_result_t *results = NULL;
    db_net_found_t found[FOUND_MAX];
    size_t n = 0;
    esp_err_t err = mdns_query_ptr("_companion-satellite-tcp", "_tcp", 2000, FOUND_MAX, &results);
    if (err == ESP_OK) {
        for (mdns_result_t *r = results; r && n < FOUND_MAX; r = r->next) {
            /* a host also announces its Docker and VPN addresses: one in our subnet first */
            const esp_ip4_addr_t *pick = NULL;
            for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
                if (a->addr.type == ESP_IPADDR_TYPE_V4 && (!pick || in_our_subnet(&a->addr.u_addr.ip4))) {
                    pick = &a->addr.u_addr.ip4;
                }
            }
            if (pick) {
                strlcpy(found[n].name, r->instance_name ? r->instance_name : "", sizeof(found[n].name));
                snprintf(found[n].host, sizeof(found[n].host), IPSTR, IP2STR(pick));
                found[n++].port = r->port;
            }
        }
        mdns_query_results_free(results);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Companion search failed: %s", esp_err_to_name(err)); /* the last list stays */
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (err == ESP_OK) {
        memcpy(s.found, found, sizeof(found));
        s.found_count = n;
    }
    s.browsing = false;
    xSemaphoreGive(s.lock);
    vTaskDelete(NULL);
}

size_t db_net_companions(db_net_found_t *out, size_t max)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (!s.browsing && (!s.browsed_at || now - s.browsed_at > BROWSE_EVERY_US)) {
        s.browsing = xTaskCreate(browse_task, "browse", 3072, NULL, 3, NULL) == pdPASS;
        s.browsed_at = now;
    }
    size_t n = s.found_count < max ? s.found_count : max;
    memcpy(out, s.found, n * sizeof(*out));
    xSemaphoreGive(s.lock);
    return n;
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

int db_net_wifi_scan(db_net_ap_t *out, size_t max)
{
    if (!s.wifi_started) {
        return -1;
    }
    wifi_scan_config_t scan = {0};
    uint16_t n = 0;
    if (esp_wifi_scan_start(&scan, true) != ESP_OK || esp_wifi_scan_get_ap_num(&n) != ESP_OK) {
        return -1;
    }
    wifi_ap_record_t *rec = calloc(n ? n : 1, sizeof(*rec));
    if (!rec) {
        esp_wifi_clear_ap_list();
        return -1;
    }
    esp_wifi_scan_get_ap_records(&n, rec); /* strongest first */
    size_t count = 0;
    for (uint16_t i = 0; i < n && count < max; i++) {
        if (!rec[i].ssid[0] || (s.st.ap_active && strcmp((char *)rec[i].ssid, s.st.ap_ssid) == 0)) {
            continue; /* hidden ones have no name to show, and the setup AP is no place to go */
        }
        db_net_ap_t *a = &out[count++];
        strlcpy(a->ssid, (char *)rec[i].ssid, sizeof(a->ssid));
        snprintf(a->bssid, sizeof(a->bssid), "%02x:%02x:%02x:%02x:%02x:%02x", rec[i].bssid[0], rec[i].bssid[1],
                 rec[i].bssid[2], rec[i].bssid[3], rec[i].bssid[4], rec[i].bssid[5]);
        a->rssi = rec[i].rssi;
        a->channel = rec[i].primary;
        a->open = rec[i].authmode == WIFI_AUTH_OPEN;
    }
    free(rec);
    return (int)count;
}

typedef struct {
    char ssid[33];
    char pass[65];
    char bssid[18];
} join_t;

static void set_join(const char *state, const char *error)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.st.join_state = state;
    strlcpy(s.st.join_error, error, sizeof(s.st.join_error));
    xSemaphoreGive(s.lock);
    changed();
}

static void join_task(void *arg)
{
    join_t *j = arg;
    const db_config_t *c = db_config();
    ESP_LOGI(TAG, "WLAN trying %s", j->ssid);
    wifi_config_t cfg = sta_config(j->ssid, j->pass, j->bssid);
    s.join_reason = 0;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
    bool up = false;
    for (int i = 0; i < 150 && !up; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        up = s.st.wlan_up && strcmp(s.st.wlan_ssid, j->ssid) == 0;
    }
    if (up) {
        db_config_t *next = heap_caps_malloc(sizeof(*next), MALLOC_CAP_SPIRAM);
        bool saved = false;
        if (next) {
            db_config_copy(next);
            strlcpy(next->wifi_ssid, j->ssid, sizeof(next->wifi_ssid));
            strlcpy(next->wifi_pass, j->pass, sizeof(next->wifi_pass));
            strlcpy(next->wifi_bssid, j->bssid, sizeof(next->wifi_bssid));
            saved = db_config_save(next) == ESP_OK;
            free(next);
        }
        set_join(saved ? "ok" : "failed", saved ? "" : "not saved");
    } else {
        int r = s.join_reason;
        set_join("failed", r == WIFI_REASON_NO_AP_FOUND                                    ? "network not found"
                           : r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                     r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_MIC_FAILURE
                               ? "wrong password"
                               : "no address");
        ESP_LOGW(TAG, "WLAN %s failed (reason %d), back to %s", j->ssid, r, c->wifi_ssid[0] ? c->wifi_ssid : "none");
        wifi_config_t old = sta_config(c->wifi_ssid, c->wifi_pass, c->wifi_bssid);
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &old);
        if (c->wifi_ssid[0]) {
            esp_wifi_connect();
        }
    }
    free(j);
    vTaskDelete(NULL);
}

esp_err_t db_net_wifi_join(const char *ssid, const char *pass, const char *bssid)
{
    uint8_t mac[6];
    if (!s.wifi_started || !ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64 || (bssid[0] && !db_config_bssid(bssid, mac))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.st.join_state && strcmp(s.st.join_state, "testing") == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    join_t *j = calloc(1, sizeof(*j));
    if (!j) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(j->ssid, ssid, sizeof(j->ssid));
    strlcpy(j->pass, pass, sizeof(j->pass));
    strlcpy(j->bssid, bssid, sizeof(j->bssid));
    set_join("testing", "");
    if (xTaskCreate(join_task, "wifi_join", 4096, j, 4, NULL) != pdPASS) {
        free(j);
        set_join("failed", "no memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
