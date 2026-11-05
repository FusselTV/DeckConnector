#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DB_MAX_SERVERS 2

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
    char servers[DB_MAX_SERVERS][96]; /* ws://host:port/deckbridge; empty: find servers with mDNS */
    char token[48];                   /* sent to servers as Bearer token, optional */
    char admin_pass[24];              /* web UI password, generated on first start */
    bool setup_done;                  /* the welcome steps on the deck or in the web UI were finished */
} db_config_t;

esp_err_t db_config_load(void);
const db_config_t *db_config(void);
/* Copy of the current config to edit and pass to db_config_save(). */
void db_config_copy(db_config_t *out);
esp_err_t db_config_save(const db_config_t *cfg);
esp_err_t db_config_reset(void);
/* Called after every successful save, from the saving task. */
void db_config_on_save(void (*cb)(void));

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
