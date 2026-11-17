#pragma once

#include <stddef.h>
#include <stdint.h>

/* Keep the newest log output in RAM so the web UI can show it. The serial console keeps working. */
void db_log_start(void);
/* Copy the buffered log, oldest line first. Returns the length. */
size_t db_log_read(char *buf, size_t cap);
/* Copy up to `cap` bytes written since `*pos` (0: the oldest still kept) and move `*pos` on. */
size_t db_log_since(uint64_t *pos, char *buf, size_t cap);
/* The end of the log the boot before this one left, 0 bytes after a power cut. */
size_t db_log_prev(char *buf, size_t cap);

/* Things worth a timeline (a source came or went, a roam, pairing, an update): logged like ESP_LOGI under
 * `tag` and kept in a ring of the last DB_EVENTS. `kind` is a short word such as "source" or "wlan". The last
 * DB_EVENTS_KEPT also go to flash, so the next boots show them too. */
#define DB_EVENTS      64
#define DB_EVENTS_KEPT 40
typedef struct {
    int64_t at_us;  /* esp_timer time of its boot */
    int64_t unix_s; /* 0 before the clock was set */
    uint8_t boot;   /* 0 this boot, 1 the one before, ... */
    char kind[12];
    char text[84];
} db_event_t;
void db_log_event(const char *tag, const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/* Oldest first. */
size_t db_log_events(db_event_t *out, size_t max);

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
