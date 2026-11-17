#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Which of the two WLANs to join. The other one is tried once the one in use gave no link for
 * DB_WLAN_FAILOVER_MS. On the backup a scan looks for the primary every wait; a move back that does not last
 * one wait returns to the backup at its first failure and doubles the wait.
 */

#define DB_WLAN_FAILOVER_MS  10000
#define DB_WLAN_CHECK_MS     120000
#define DB_WLAN_CHECK_MAX_MS 1920000

typedef struct {
    bool backup; /* the backup is in use or being joined */
    bool down;
    bool back;        /* left the backup for the primary at back_ms */
    int64_t since_ms; /* down: when the link went; on the backup: the last look for the primary */
    int64_t back_ms;
    int64_t wait_ms; /* 0: DB_WLAN_CHECK_MS */
} db_wlan_t;

void db_wlan_up(db_wlan_t *w, int64_t now);
/* A failed join or a lost link. True: join the network `backup` names now. */
bool db_wlan_lost(db_wlan_t *w, int64_t now, bool have_backup);
bool db_wlan_check_due(const db_wlan_t *w, int64_t now);
/* True: leave the backup for the primary now. */
bool db_wlan_scanned(db_wlan_t *w, int64_t now, bool primary_seen);
