#include "db_wlan.h"

static int64_t wait_of(const db_wlan_t *w)
{
    return w->wait_ms ? w->wait_ms : DB_WLAN_CHECK_MS;
}

void db_wlan_start(db_wlan_t *w, int64_t now)
{
    *w = (db_wlan_t){.down = true, .since_ms = now};
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
        int64_t held = now - w->back_ms;
        if (have_backup && held < wait_of(w)) {
            int64_t next = wait_of(w) * 2;
            w->wait_ms = next < DB_WLAN_CHECK_MAX_MS ? next : DB_WLAN_CHECK_MAX_MS;
            go = true;
        } else if (held >= DB_WLAN_HOLD_MS) {
            w->wait_ms = 0;
            w->back = false;
        }
    }
    if (go) {
        w->backup = !w->backup;
        w->back = false;
        w->since_ms = now;
    }
    return go;
}

bool db_wlan_check_due(const db_wlan_t *w, int64_t now)
{
    return w->backup && !w->down && now - w->since_ms >= wait_of(w);
}

bool db_wlan_scanned(db_wlan_t *w, int64_t now, int rssi)
{
    w->since_ms = now;
    if (rssi < DB_WLAN_BACK_RSSI) {
        return false;
    }
    w->backup = false;
    w->back = true;
    w->back_ms = now;
    return true;
}

uint32_t db_wlan_retry_ms(uint32_t last_ms, bool switched, bool after_link)
{
    if (switched && after_link) {
        return 250;
    }
    uint32_t next = last_ms ? (last_ms * 2 > 8000 ? 8000 : last_ms * 2) : 250;
    return switched && next < 2000 ? 2000 : next;
}
