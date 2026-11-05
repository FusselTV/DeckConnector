#pragma once

#include <stddef.h>
#include <stdint.h>

/* Keep the newest log output in RAM so the web UI can show it. The serial console keeps working. */
void db_log_start(void);
/* Copy the buffered log, oldest line first. Returns the length. */
size_t db_log_read(char *buf, size_t cap);

/* The milestones of this boot: the first time each one happened, in ms since boot. For boot time tests. */
typedef enum {
    DB_MARK_DECK,       /* a Stream Deck is ready */
    DB_MARK_LAN_LINK,   /* the LAN cable has a link */
    DB_MARK_LAN_IP,     /* the LAN has an address */
    DB_MARK_WLAN_IP,    /* the WLAN has an address */
    DB_MARK_COMPANION,  /* Companion uses the deck */
    DB_MARK_APP,        /* the Stream Deck app uses the deck */
    DB_MARK_SERVER,     /* a server is connected */
    DB_MARK_HOST_DRAWS, /* the deck shows a host's picture, not its own screen */
    DB_MARK_SATELLITE,  /* Companion took the deck over its Satellite API */
    DB_MARK_COUNT,
} db_mark_t;

void db_log_mark(db_mark_t mark);
/* 0 when it has not happened yet. */
int64_t db_log_mark_ms(db_mark_t mark);
const char *db_log_mark_name(db_mark_t mark);
