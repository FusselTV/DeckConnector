#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DB_MAX_SERVERS 2

typedef enum {
    DB_MODE_NATIVE = 0, /* deckbridge servers over WebSocket */
    DB_MODE_DOCK = 1,   /* act like an Elgato Network Dock for Companion and the Stream Deck app */
} db_mode_t;

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
    db_mode_t mode;
    uint8_t brightness;
    db_ip_config_t lan;
    db_ip_config_t wlan;
    char wifi_ssid[33];
    char wifi_pass[65];
    char servers[DB_MAX_SERVERS][96]; /* ws://host:port/deckbridge; empty: find servers with mDNS */
    char token[48];                   /* sent to servers as Bearer token, optional */
    char admin_pass[24];              /* web UI password, generated on first start */
} db_config_t;

esp_err_t db_config_load(void);
const db_config_t *db_config(void);
/* Copy of the current config to edit and pass to db_config_save(). */
void db_config_copy(db_config_t *out);
esp_err_t db_config_save(const db_config_t *cfg);
esp_err_t db_config_reset(void);

/* `len - 1` characters without look-alikes (no l, 1, o, 0), NUL terminated. */
void db_config_random_password(char *out, size_t len);

/*
 * JSON for the web UI and servers. Secrets are left out unless `with_secrets`.
 * db_config_from_json() applies only the keys that are present.
 */
size_t db_config_to_json(const db_config_t *cfg, bool with_secrets, char *buf, size_t cap);
esp_err_t db_config_from_json(db_config_t *cfg, const char *json, char *err, size_t err_cap);
