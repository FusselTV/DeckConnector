#include "db_wlan.h"

static int64_t wait_of(const db_wlan_t *w)
{
    return w->wait_ms ? w->wait_ms : DB_WLAN_CHECK_MS;
}

void db_wlan_up(db_wlan_t *w, int64_t now)
{
    w->down = false;
    w->since_ms = now;
}

bool db_wlan_lost(db_wlan_t *w, int64_t now, bool have_backup)
{
    if (!w->down) {
        w->down = true;
        w->since_ms = now;
    }
    bool go = have_backup ? now - w->since_ms >= DB_WLAN_FAILOVER_MS : w->backup;
    if (w->back) {
        w->back = false;
        if (have_backup && now - w->back_ms < wait_of(w)) {
            int64_t next = wait_of(w) * 2;
            w->wait_ms = next < DB_WLAN_CHECK_MAX_MS ? next : DB_WLAN_CHECK_MAX_MS;
            go = true;
        } else {
            w->wait_ms = 0;
        }
    }
    if (go) {
        w->backup = !w->backup;
        w->since_ms = now;
    }
    return go;
}

bool db_wlan_check_due(const db_wlan_t *w, int64_t now)
{
    return w->backup && !w->down && now - w->since_ms >= wait_of(w);
}

bool db_wlan_scanned(db_wlan_t *w, int64_t now, bool primary_seen)
{
    w->since_ms = now;
    if (!primary_seen) {
        return false;
    }
    w->backup = false;
    w->back = true;
    w->back_ms = now;
    return true;
}
