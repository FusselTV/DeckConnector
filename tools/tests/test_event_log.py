"""The event timeline outlives a restart: the last events come back from flash, marked with the boot they belong to."""

import json
import random
import time
import urllib.request

from conftest import HOST, api, wait_for_bridge


def events() -> list[dict]:
    code, body = api("GET", "/api/events")
    assert code == 200
    return json.loads(body)


def restart() -> None:
    time.sleep(3)  # the bridge writes new events to flash within 2 s
    assert api("POST", "/api/reboot")[0] == 200
    time.sleep(5)
    wait_for_bridge()


def test_events_come_back_after_restarts_with_their_boot():
    now = [e["text"] for e in events() if not e.get("boot")]
    assert now, "this boot has no events"
    restart()
    first = events()
    earlier = [e["text"] for e in first if e.get("boot") == 1]
    assert earlier[-len(now[-10:]) :] == now[-10:], "the last events of the boot before are missing or out of order"
    assert all(e["since_s"] >= 0 for e in first if e.get("boot")), "an earlier boot's event lost its time since start"
    assert any(e["kind"] == "boot" and not e.get("boot") for e in first), "the new boot logged no start"
    restart()
    assert any(e.get("boot") == 2 for e in events()), "two restarts back is gone"


def test_settings_storage_keeps_room():
    m = json.loads(api("GET", "/api/metrics")[1])["flash"]["settings"]
    assert m["used"] < m["size"] * 0.6, f"settings storage {m['used']} of {m['size']} bytes"


def setup_request(ip: str) -> None:
    """Companion's Setup on a set-up bridge: the deck asks, and the bridge logs an event for it."""
    time.sleep(2.1)  # one Setup per 2 s
    req = urllib.request.Request(
        f"http://{HOST}/api/config",
        data=json.dumps({"host": ip, "port": 16622}).encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    urllib.request.urlopen(req, timeout=10).read()


def test_the_last_event_before_a_restart_is_kept():
    """The bridge writes at most every 30 s: the second event waits in that pause, and the restart writes it."""
    ip = f"192.0.2.{random.randint(10, 250)}"
    setup_request(ip)  # written at once
    setup_request(ip)  # written only on the way down
    assert api("POST", "/api/reboot")[0] == 200
    time.sleep(5)
    wait_for_bridge()
    kept = [e for e in events() if e.get("boot") == 1 and ip in e["text"]]
    assert len(kept) == 2, f"{len(kept)} of the 2 events before the restart kept"
