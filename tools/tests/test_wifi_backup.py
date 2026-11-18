"""The primary WLAN goes off, the bridge moves to the backup WLAN and back once the primary is there again.
Adds a WLAN to the UniFi controller and turns the bridge's off for minutes, so only with `pytest -m unifi`. The
bridge stays reachable over the LAN throughout.
"""

import base64
import json
import os
import secrets
import subprocess
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, PASSWORD, api, status

from dbtools.poe import KEY, SITE, TLS, call

pytestmark = [pytest.mark.unifi, pytest.mark.skipif(not (SITE and KEY.exists()), reason="no UniFi controller")]
BACKUP = "DC-Backup"
CHECK_S = 120


def wlans() -> list[dict]:
    return call("rest/wlanconf")["data"]


def unifi(method: str, path: str, body: dict | None = None) -> dict:
    req = urllib.request.Request(
        f"{SITE}/{path}",
        data=json.dumps(body).encode() if body is not None else None,
        method=method,
        headers={"X-API-KEY": KEY.read_text().strip(), "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, context=TLS, timeout=15) as r:
        return json.loads(r.read())


def wlan_state() -> dict:
    return status()["network"]["wlan"]


def wait(cond, secs: float, every: float = 0.5):
    end = time.monotonic() + secs
    while time.monotonic() < end:
        try:
            if v := cond():
                return v
        except OSError:
            pass
        time.sleep(every)
    return None


def mac_of(ip: str) -> str:
    subprocess.run(["ping", "-c1", "-W1", ip], capture_output=True)
    out = subprocess.run(["ip", "neigh", "show", ip], capture_output=True, text=True).stdout.split()
    return out[out.index("lladdr") + 1] if "lladdr" in out else ""


def reachable_over_wlan(ip: str) -> bool:
    """The page answers at the WLAN address, and that address is the radio's, not the LAN port's."""
    auth = "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    req = urllib.request.Request(f"http://{ip}/api/status", headers={"X-DeckConnector": "1", "Authorization": auth})
    with urllib.request.urlopen(req, timeout=3) as r:
        ok = json.loads(r.read())["network"]["wlan"]["ip"] == ip
    return ok and mac_of(ip) not in ("", mac_of(HOST))


def wlan_events(since: float) -> list[str]:
    evs = json.loads(api("GET", "/api/events")[1])
    return [e["text"] for e in evs if e["kind"] == "wlan" and not e.get("boot") and e["since_s"] >= since]


def test_the_backup_takes_over_and_hands_back():
    cfg = json.loads(api("GET", "/api/config")[1])
    primary = cfg["wifi_ssid"]
    w = wlan_state()
    if not (primary and w["ip"] and not w.get("backup") and status()["network"]["lan"]["ip"]):
        pytest.skip("needs the bridge on its primary WLAN with the LAN up")
    full = json.loads(api("GET", "/api/backup?secrets=1")[1])
    keep = Path("~/.config/deckconnector").expanduser() / f"backup-{time.strftime('%Y%m%d-%H%M%S')}.json"
    keep.parent.mkdir(mode=0o700, exist_ok=True)
    fd = os.open(keep, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(full, f)
    own = next(x for x in wlans() if x["name"] == primary)
    assert not any(x["name"] == BACKUP for x in wlans()), f"{BACKUP} left over from an earlier run"
    password = secrets.token_urlsafe(18)
    copy = ("networkconf_id", "usergroup_id", "ap_group_ids", "ap_group_mode", "wlan_band", "wlan_bands", "pmf_mode")
    created = None
    try:
        created = unifi(
            "POST",
            "rest/wlanconf",
            {k: own[k] for k in copy if k in own}
            | {
                "name": BACKUP,
                "enabled": True,
                "hide_ssid": False,
                "security": "wpapsk",
                "wpa_mode": "wpa2",
                "wpa_enc": "ccmp",
                "x_passphrase": password,
            },
        )["data"][0]
        code, body = api("PUT", "/api/config", json.dumps({"wifi2_ssid": BACKUP, "wifi2_pass": password}).encode())
        assert code == 200 and json.loads(body)["restart"] is False, body
        seen = wait(
            lambda: any(a["ssid"] == BACKUP for a in json.loads(api("GET", "/api/wifi/scan", timeout=15)[1])), 180, 5
        )
        assert seen, f"{BACKUP} is on the air"
        assert wlan_state()["ssid"] == primary, "a scan from the page moves nothing"
        since = status().get("uptime_s", 0)

        unifi("PUT", f"rest/wlanconf/{own['_id']}", {"enabled": False})
        t0 = time.monotonic()
        assert wait(lambda: wlan_state()["ssid"] != primary or not wlan_state()["ip"], 120), "the primary went"
        t_gone = time.monotonic()
        on_backup = wait(lambda: (s := wlan_state())["ip"] and s.get("backup") and s["ssid"] == BACKUP and s, 90)
        t_backup = time.monotonic()
        assert on_backup, f"on the backup: {wlan_state()}"
        print(f"\nprimary off: link gone after {t_gone - t0:.1f} s, on {BACKUP} {t_backup - t_gone:.1f} s later")
        assert t_backup - t_gone < 30
        assert wait(lambda: reachable_over_wlan(on_backup["ip"]), 20), "reachable over the backup WLAN"
        assert wait(lambda: any("trying the backup" in e for e in wlan_events(since)), 5), wlan_events(since)

        assert api("PUT", "/api/config", json.dumps({"wifi2_ssid": "", "wifi2_pass": ""}).encode())[0] == 200
        assert wait(lambda: wlan_state()["ssid"] != BACKUP or not wlan_state()["ip"], 15), "a removed backup is left"
        assert api("PUT", "/api/config", json.dumps({"wifi2_ssid": BACKUP, "wifi2_pass": password}).encode())[0] == 200
        t_readd = time.monotonic()
        assert wait(lambda: (s := wlan_state())["ip"] and s.get("backup") and s, 60), "on the backup again"
        t_backup = time.monotonic()
        print(f"backup removed and set again while on it: on {BACKUP} again {t_backup - t_readd:.1f} s later")

        unifi("PUT", f"rest/wlanconf/{own['_id']}", {"enabled": True})
        t1 = time.monotonic()
        back = wait(
            lambda: (s := wlan_state())["ip"] and s["ssid"] == primary and not s.get("backup") and s, CHECK_S + 60, 1
        )
        t_back = time.monotonic()
        assert back, f"back on {primary}: {wlan_state()}"
        print(f"primary on: back on {primary} {t_back - t1:.1f} s later, {t_back - t_backup:.1f} s after the switch")
        assert t_back - t_backup < CHECK_S + 45, "within one check interval of joining the backup"
        assert wait(lambda: reachable_over_wlan(back["ip"]), 20), "reachable over the primary WLAN"
        changes = 0
        last = (back["ssid"], back["ip"])
        end = time.monotonic() + 60
        while time.monotonic() < end:
            s = wlan_state()
            changes += (s["ssid"], s["ip"]) != last
            last = (s["ssid"], s["ip"])
            time.sleep(1)
        evs = wlan_events(since)
        print("\n".join(evs))
        assert changes == 0, "no flapping after the move back"
        assert sum("trying the backup" in e for e in evs) == 2 and sum("is back" in e for e in evs) == 1, evs
    finally:
        try:
            unifi("PUT", f"rest/wlanconf/{own['_id']}", {"enabled": True})
        finally:
            if created:
                unifi("DELETE", f"rest/wlanconf/{created['_id']}")
            back = {"wifi2_ssid": full.get("wifi2_ssid", ""), "wifi2_pass": full.get("wifi2_pass", "")}
            api("PUT", "/api/config", json.dumps(back).encode())
    assert not any(x["name"] == BACKUP for x in wlans())
    assert next(x for x in wlans() if x["_id"] == own["_id"])["enabled"]
    now = json.loads(api("GET", "/api/backup?secrets=1")[1])
    assert {k: now[k] for k in ("wifi_ssid", "wifi_pass", "wifi_bssid", "wlan")} == {
        k: full[k] for k in ("wifi_ssid", "wifi_pass", "wifi_bssid", "wlan")
    }, "the primary WLAN settings are as before"
