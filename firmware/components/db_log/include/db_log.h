#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "db_evring.h"
#include "db_json.h"

/* Keep the newest log output in RAM so the web UI can show it. The serial console keeps working. */
void db_log_start(void);
/* Copy the buffered log, oldest line first. Returns the length. */
size_t db_log_read(char *buf, size_t cap);
/* Copy up to `cap` bytes written since `*pos` (0: the oldest still kept) and move `*pos` on. */
size_t db_log_since(uint64_t *pos, char *buf, size_t cap);
/* The end of the log the boot before this one left, 0 bytes after a power cut. */
size_t db_log_prev(char *buf, size_t cap);
/* Where log lines go in place of stdout, each in one call: stdout takes the serial port's lock per byte. */
void db_log_set_sink(void (*write)(const char *line, size_t len));

/*
 * Things worth a timeline (a source came or went, a roam, pairing, an update): logged like ESP_LOGI under
 * `tag` and numbered. The page translates `code` and its args; DB_EVENTS stay in RAM, and once the settings
 * storage is up they go to the `evlog` partition, or the last DB_EVENTS_KEPT to the settings storage on a
 * partition table without it.
 */
#define DB_EVENTS      64
#define DB_EVENTS_KEPT 40

/* Kept in flash by number: new codes go last, before DB_EV_COUNT. */
typedef enum {
    DB_EV_TEXT, /* from a firmware before event codes: its English text */
    DB_EV_MORE, /* the rest of the text before it */
    DB_EV_STARTED,
    DB_EV_CRASHED,
    DB_EV_SETTINGS_ERASED,
    DB_EV_SETTINGS_FAILED,
    DB_EV_START_FAILED,
    DB_EV_UPDATE_ABORTED,
    DB_EV_UPDATE_INSTALLED,
    DB_EV_UPDATE_RESTART,
    DB_EV_UPDATE_CONFIRMED,
    DB_EV_ROLLBACK_UNHEALTHY,
    DB_EV_ROLLBACK_UNCONFIRMED,
    DB_EV_SERVER_LOST,
    DB_EV_SERVER_CONNECTED,
    DB_EV_COMPANION_LOST,
    DB_EV_COMPANION_SILENT,
    DB_EV_COMPANION_CONNECTED,
    DB_EV_HOST_CONNECTED,
    DB_EV_HOST_LEFT,
    DB_EV_DOCK_REFUSED,
    DB_EV_NO_SATELLITE,
    DB_EV_MOVED_TO_SATELLITE,
    DB_EV_COMPANION_ASKS,
    DB_EV_COMPANION_SETUP,
    DB_EV_PAIR_ASKS,
    DB_EV_PAIR_ACCEPTED,
    DB_EV_PAIR_REFUSED,
    DB_EV_DECK_PARKED,
    DB_EV_SCREEN_OFF,
    DB_EV_SCREEN_DIMMED,
    DB_EV_SCREEN_ON,
    DB_EV_LAN_DEAD,
    DB_EV_LAN_BACK,
    DB_EV_LAN_DOWN,
    DB_EV_WLAN_BACK,
    DB_EV_WLAN_ROAM,
    DB_EV_WLAN_JOINED,
    DB_EV_WLAN_NO_LINK,
    DB_EV_WLAN_NO_LINK_BACKUP,
    DB_EV_MATTER_PAIRED,
    DB_EV_MATTER_LEFT,
    DB_EV_MATTER_STORAGE,
    DB_EV_MATTER_NO_CODE,
    DB_EV_MATTER_NO_MEMORY,
    DB_EV_MATTER_FAILED,
    DB_EV_COUNT,
} db_ev_t;

typedef db_evrec_t db_event_t;

/* The args are strings: db_log_event(TAG, DB_EV_COMPANION_LOST, strerror(errno)). */
/* NOLINTNEXTLINE(readability-identifier-naming): it reads as the function it wraps */
#define db_log_event(tag, code, ...)                                                                         \
    db_log_event_args(tag, code, (const char *const[]){__VA_ARGS__ __VA_OPT__(, ) NULL})
void db_log_event_args(const char *tag, db_ev_t code, const char *const *args);
/* Once the settings storage is up: the earlier boots' events come back, and new ones are kept. */
void db_log_keep_start(void);
bool db_log_event_get(uint32_t seq, db_event_t *out);
/* The numbers of the oldest and newest event kept, 0 when there is none. */
void db_log_events_range(uint32_t *oldest, uint32_t *newest);
/* Where events outlive a restart: "flash", "settings" or "ram" (before db_log_keep_start or without storage).
 */
const char *db_log_events_store(void);
/* One event as a JSON object: seq, at, since_s, boot (0 this one, 1 the one before...), kind, code, level,
 * args and the English text. */
void db_log_event_json(db_json_t *j, const char *key, const db_event_t *e);
/* Called with each new event's number, in the task that logged it. */
void db_log_on_event(void (*cb)(uint32_t seq));

/* The milestones of this boot: the first time each one happened, in ms since boot. For boot time tests. */
typedef enum {
    DB_MARK_DECK, /* a deck is ready */
    DB_MARK_LAN_LINK,
    DB_MARK_LAN_IP,
    DB_MARK_WLAN_IP,
    DB_MARK_COMPANION,
    DB_MARK_APP,
    DB_MARK_SERVER,
    DB_MARK_HOST_DRAWS, /* the deck shows a host's picture, not its own screen */
    DB_MARK_SATELLITE,
    DB_MARK_COUNT,
} db_mark_t;

void db_log_mark(db_mark_t mark);
/* 0 when it has not happened yet. */
int64_t db_log_mark_ms(db_mark_t mark);
const char *db_log_mark_name(db_mark_t mark);
