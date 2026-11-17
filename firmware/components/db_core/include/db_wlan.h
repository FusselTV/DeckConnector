#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Which of the two WLANs to join. The other one is tried once the one in use gave no address for
 * DB_WLAN_FAILOVER_MS. On the backup a scan looks for the primary every wait and moves back when it is at
 * DB_WLAN_BACK_RSSI or better. A move back that does not last one wait returns to the backup at its first
 * failure and doubles the wait; it is back to DB_WLAN_CHECK_MS once the primary held for DB_WLAN_HOLD_MS.
 */

#define DB_WLAN_FAILOVER_MS  10000
#define DB_WLAN_CHECK_MS     120000
#define DB_WLAN_CHECK_MAX_MS 1920000
#define DB_WLAN_HOLD_MS      1800000
#define DB_WLAN_BACK_RSSI    (-70)

typedef struct {
    bool backup; /* the backup is in use or being joined */
    bool down;
    bool back;        /* left the backup for the primary at back_ms */
    int64_t since_ms; /* down: when the address went; on the backup: the last look for the primary */
    int64_t back_ms;
    int64_t wait_ms; /* 0: DB_WLAN_CHECK_MS */
} db_wlan_t;

/* On the primary with no address yet. */
void db_wlan_start(db_wlan_t *w, int64_t now);
void db_wlan_up(db_wlan_t *w, int64_t now);
/* A failed join, a lost link or a join without an address. True: join the network `backup` names now. */
bool db_wlan_lost(db_wlan_t *w, int64_t now, bool have_backup);
bool db_wlan_check_due(const db_wlan_t *w, int64_t now);
/* The primary's best signal in the scan, or -128 when it is not there. True: leave the backup for it now. */
bool db_wlan_scanned(db_wlan_t *w, int64_t now, int rssi);
/* The wait before the next join after `last_ms`. A switch right after a working link tries at once; a switch
 * while both are gone waits at least 2 s, as every try is a scan of all channels. */
uint32_t db_wlan_retry_ms(uint32_t last_ms, bool switched, bool after_link);
