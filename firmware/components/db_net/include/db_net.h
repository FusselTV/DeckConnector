#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool lan_link;
    bool lan_up;   /* has an address */
    bool lan_dead; /* address and link, but the gateway stopped answering */
    bool wlan_up;
    bool wlan_chip;
    bool ap_active;
    int rssi;
    char lan_ip[16];
    char wlan_ip[16];
    char ap_ssid[33];
    char ap_pass[16];
    char ap_ip[16];
    char wlan_ssid[33]; /* the network joined, and the access point and channel */
    char wlan_bssid[18];
    uint8_t wlan_channel;
    const char *join_state; /* a join from db_net_wifi_join: "", "testing", "ok" or "failed" */
    char join_error[32];
} db_net_status_t;

typedef struct {
    char ssid[33];
    char bssid[18];
    int8_t rssi;
    uint8_t channel;
    bool open;
} db_net_ap_t;

/* Start Ethernet and WLAN with the saved config, and mDNS as <config name>.local. */
esp_err_t db_net_start(void);
void db_net_status(db_net_status_t *out);
bool db_net_online(void);

/* lwIP name of the WLAN or LAN interface while it has an address, for SO_BINDTODEVICE. False otherwise. */
bool db_net_ifname(bool wlan, char name[6]);

/* The interface `*wlan` names if it has an address, else the other one (and `*wlan` says which). */
bool db_net_pick(bool *wlan, char name[6]);
/* True while the LAN or the WLAN has this address (network order). */
bool db_net_has_ip(uint32_t ip);
/* A SYN from an old local address and port to a peer (all network order), to end a session of the last boot: the
 * peer's stack answers with an ACK, ours with a reset. The non-blocking socket, or -1 when the old port cannot be
 * bound; the caller closes it once the reset is out. */
int db_net_syn_from(uint32_t local_ip, uint16_t local_port, uint32_t ip, uint16_t port);

/* Called from the event task whenever an address or link changes. One listener. */
void db_net_on_change(void (*cb)(void));

typedef struct {
    char name[32];
    char host[16];
    uint16_t port;
} db_net_found_t;

/* Companions that announce their Satellite API over mDNS, from the last browse. A call starts a new 2 s browse in
 * the background when the last one is older than 5 s, so the next call has fresh results. */
size_t db_net_companions(db_net_found_t *out, size_t max);

/* Networks in reach, one entry per access point, strongest first, without the setup AP. -1 while the radio is busy. */
int db_net_wifi_scan(db_net_ap_t *out, size_t max);
/* Try a network in the background; it is saved only when it gives an address within 15 s, otherwise the old one
 * comes back. The state shows in db_net_status(). */
esp_err_t db_net_wifi_join(const char *ssid, const char *pass, const char *bssid);
/* Setup access point "<name>" with a fresh random WPA2 password. */
esp_err_t db_net_ap_start(void);
esp_err_t db_net_ap_stop(void);
/* Phones joined to the setup access point. */
int db_net_ap_clients(void);
