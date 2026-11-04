#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool lan_link;
    bool lan_up; /* has an address */
    bool wlan_up;
    bool ap_active;
    int rssi;
    char lan_ip[16];
    char wlan_ip[16];
    char ap_ssid[33];
    char ap_pass[16];
    char ap_ip[16];
} db_net_status_t;

/* Start Ethernet and WLAN with the saved config, and mDNS as <config name>.local. */
esp_err_t db_net_start(void);
void db_net_status(db_net_status_t *out);
bool db_net_online(void);

/* lwIP name of the WLAN or LAN interface while it has an address, for SO_BINDTODEVICE. False otherwise. */
bool db_net_ifname(bool wlan, char name[6]);

/* Called from the event task whenever an address or link changes. One listener. */
void db_net_on_change(void (*cb)(void));

/* Setup access point "<name>" with a fresh random WPA2 password. */
esp_err_t db_net_ap_start(void);
esp_err_t db_net_ap_stop(void);
