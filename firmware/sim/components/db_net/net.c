#include <stdlib.h>
#include <string.h>

#include "db_config.h"
#include "db_net.h"
#include "esp_event.h"

size_t db_net_companions(db_net_found_t *out, size_t max)
{
    db_net_found_t sim = {.name = "Companion (sim)", .host = "127.0.0.1", .port = 16622};
    if (max) {
        out[0] = sim;
    }
    return max ? 1 : 0;
}

static const char *sim_join = "";

/* DB_SIM_VIA_AP=1: the setup AP is on and has the loopback address, so every request counts as over the AP.
 */
void db_net_status(db_net_status_t *out)
{
    *out = (db_net_status_t){.lan_link = true, .lan_up = true, .join_state = sim_join};
    strcpy(out->lan_ip, "127.0.0.1");
    if (getenv("DB_SIM_VIA_AP")) {
        out->ap_active = true;
        strcpy(out->ap_ip, "127.0.0.1");
    }
}

esp_err_t db_net_ap_stop(void)
{
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

esp_err_t db_net_start(void)
{
    return esp_event_loop_create_default(); /* esp_http_server posts its events there */
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
