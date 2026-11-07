#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DB_MAX_SERVERS 4

/* Who draws on the deck and gets its input. Switching needs no restart. */
typedef enum {
    DB_CTRL_SERVER = 0,    /* deckbridge servers over WebSocket */
    DB_CTRL_COMPANION = 1, /* Bitfocus Companion, through the Network Dock emulation; was "dock mode" */
    DB_CTRL_APP = 2,       /* the Elgato Stream Deck app, through the Network Dock emulation */
    DB_CTRL_COUNT,
} db_controller_t;

typedef struct {
    bool dhcp;
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
} db_ip_config_t;

/* Saved as one NVS blob. Only append fields; db_config_load() fills new ones with defaults. */
typedef struct {
    uint32_t version;
    char name[32];
    db_controller_t controller;
    uint8_t brightness;
    db_ip_config_t lan;
    db_ip_config_t wlan;
    char wifi_ssid[33];
    char wifi_pass[65];
    char servers_first[2][96]; /* use db_config_server(); the blob layout of older firmware */
    char token[48];            /* sent to servers as Bearer token, optional */
    char admin_pass[24];       /* web UI password, generated on first start */
    bool setup_done;           /* the welcome steps on the deck or in the web UI were finished */
    char servers_more[DB_MAX_SERVERS - 2][96];
    char host[96]; /* which one of `controller` draws: a dock host's IP, a server's URL or "satellite" */
    bool
        dock_paired_only; /* Companion and app hosts must be paired; off: anyone on the network may connect */
    char satellite[64];   /* Companion's Satellite API: "host", "host:port", ws:// or wss:// URL; empty: not
                             used */
    bool https_only;      /* plain HTTP only redirects browsers to HTTPS and answers nothing else */
    /* SHA-256 of a wss:// server's certificate in hex, one per server and one for Satellite; empty: a public
     * CA must vouch for the certificate */
    char server_pins[DB_MAX_SERVERS][65];
    char satellite_pin[65];
    char cors_origins[128]; /* origins whose pages may call the API with a token, comma separated; empty: none
                             */
    char menu_chord[10];   /* which two controls open the menu: auto, keys, encoders or off */
    uint16_t menu_hold_ms;
    char usb_speed[6]; /* auto: high speed, full speed when no deck comes up behind a hub; full: always */
    char wifi_bssid[18]; /* "aa:bb:cc:dd:ee:ff" joins only that access point; empty: any with wifi_ssid */
    char ntp_server[64]; /* empty: the one DHCP names, else pool.ntp.org */
    bool input_lock;     /* unused: keeps the layout of blobs saved by older firmware */
    bool qr_login_off;   /* JSON qr_login: false; the welcome pages show no one-time login code */
} db_config_t;

/* Server URL `i` (ws://host:port/deckbridge), empty when unused. With none set, servers are found by mDNS. */
char *db_config_server(db_config_t *cfg, int i);
const char *db_config_server_of(const db_config_t *cfg, int i);
/* A host name or address without spaces, quotes or slashes; empty is fine. */
bool db_config_ntp_ok(const char *s);
/* Empty (an open network), 8 to 63 characters, or 64 hex digits. */
bool db_config_wifi_pass_ok(const char *p);

esp_err_t db_config_load(void);
const db_config_t *db_config(void);
/* Copy of the current config to edit and pass to db_config_save(). */
void db_config_copy(db_config_t *out);
esp_err_t db_config_save(const db_config_t *cfg);
/* Around a copy, change and save that must not overwrite another task's save made in between. */
void db_config_edit_begin(void);
void db_config_edit_end(void);
/* Factory settings, without API tokens, paired hosts and an uploaded HTTPS certificate. */
esp_err_t db_config_reset(void);
/* Called after every successful save, from the saving task. */
void db_config_on_save(void (*cb)(void));

/* `hex` as 32 bytes; false when it is empty. */
bool db_config_pin(const char *hex, uint8_t out[32]);
/* "aa:bb:cc:dd:ee:ff" into 6 bytes; false for anything else, the empty text included. */
bool db_config_bssid(const char *text, uint8_t out[6]);

/* The name in the JSON. */
const char *db_controller_name(db_controller_t c);

/* `len - 1` characters without look-alikes (no l, 1, o, 0), NUL terminated. */
void db_config_random_password(char *out, size_t len);

/*
 * JSON for the web UI and servers. Secrets are left out unless `with_secrets`.
 * db_config_from_json() applies only the keys that are present.
 */
size_t db_config_to_json(const db_config_t *cfg, bool with_secrets, char *buf, size_t cap);
esp_err_t db_config_from_json(db_config_t *cfg, const char *json, char *err, size_t err_cap);
