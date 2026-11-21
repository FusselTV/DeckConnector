#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "db_config.h"
#include "db_net.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

size_t db_net_companions(db_net_found_t *out, size_t max)
{
    db_net_found_t sim = {.name = "Companion (sim)", .host = "127.0.0.1", .port = 16622};
    if (max) {
        out[0] = sim;
    }
    return max ? 1 : 0;
}

static const char *sim_join = "";
static bool ap_closed;

/* DB_SIM_VIA_AP=1: the setup AP is on and has the loopback address, so every request counts as over the AP
 * until DELETE /api/ap closes it.
 */
void db_net_status(db_net_status_t *out)
{
    *out = (db_net_status_t){.lan_link = true, .lan_up = true, .join_state = sim_join};
    strcpy(out->lan_ip, "127.0.0.1");
    if (getenv("DB_SIM_VIA_AP") && !ap_closed) {
        out->ap_active = true;
        strcpy(out->ap_ip, "127.0.0.1");
    }
}

esp_err_t db_net_ap_stop(void)
{
    ap_closed = true;
    return ESP_OK;
}

int db_net_wifi_scan(db_net_ap_t *out, size_t max)
{
    db_net_ap_t sim[] = {{"Zuhause", "aa:bb:cc:00:00:01", -48, 1, false},
                         {"Zuhause", "aa:bb:cc:00:00:02", -61, 6, false},
                         {"Gast", "aa:bb:cc:00:00:03", -70, 11, true}};
    size_t n = max < 3 ? max : 3;
    memcpy(out, sim, n * sizeof(*out));
    return (int)n;
}

esp_err_t db_net_wifi_join(const char *ssid, const char *pass, const char *bssid)
{
    uint8_t mac[6];
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64 || (bssid[0] && !db_config_bssid(bssid, mac))) {
        return ESP_ERR_INVALID_ARG;
    }
    sim_join = strcmp(ssid, "Zuhause") == 0 ? "ok" : "failed"; /* no radio: a known name joins at once */
    return ESP_OK;
}

void db_net_wifi_backup_changed(void)
{
}

/* The FreeRTOS Linux port counts a task blocked in the kernel as running, and every other task waits with it.
 * mdns's receive task calls this instead of select() (CMakeLists.txt): one look, then a tick of sleep. */
int db_sim_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    struct timeval now = {0};
    int got = select(n, r, w, e, &now);
    if (got == 0) {
        vTaskDelay(1);
    }
    return got;
}

/* The board's mDNS start. DB_SIM_MDNS_IF names the interface it answers on: never the LAN, a veth in a network
 * namespace of its own (tools/simnet). Without it the responder runs and stays silent. */
esp_err_t db_net_start(void)
{
    esp_err_t err = esp_event_loop_create_default(); /* esp_http_server posts its events there */
    err = err == ESP_OK ? mdns_init() : err;
    err = err == ESP_OK ? mdns_hostname_set(db_config()->name) : err;
    err = err == ESP_OK ? mdns_instance_name_set(db_config()->name) : err;
    const char *ifname = getenv("DB_SIM_MDNS_IF");
    if (err == ESP_OK && ifname) {
        esp_netif_inherent_config_t base = {.if_key = "ETH_DEF", .if_desc = ifname};
        esp_netif_t *lan = esp_netif_new(&(esp_netif_config_t){.base = &base});
        err = lan ? mdns_register_netif(lan) : ESP_ERR_NO_MEM;
        err = err == ESP_OK ? mdns_netif_action(lan, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4) : err;
    }
    if (err != ESP_OK) {
        abort(); /* the mDNS tests would only see a browse time out */
    }
    return ESP_OK;
}

bool db_net_online(void)
{
    return true;
}

/* No interface to bind to: sockets take the host's route. */
bool db_net_ifname(bool wlan, char name[6])
{
    return false;
}

bool db_net_pick(bool *wlan, char name[6])
{
    *wlan = false;
    return false;
}

bool db_net_path_ready(uint32_t local_ip, uint32_t ip)
{
    return true;
}

int db_net_syn_from(uint32_t local_ip, uint16_t local_port, uint32_t ip, uint16_t port)
{
    return -1;
}

void db_net_on_change(void (*cb)(void))
{
}

esp_err_t db_net_ap_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

int db_net_ap_clients(void)
{
    return 0;
}
