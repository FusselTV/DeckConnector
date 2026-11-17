"""The event timeline outlives a restart: the last events come back from flash, marked with the boot they belong to."""

import json
import time

from conftest import api, wait_for_bridge


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
    assert all(e["since_s"] is None for e in first if e.get("boot")), "an earlier boot's event has a time since start"
    assert any(e["kind"] == "boot" and not e.get("boot") for e in first), "the new boot logged no start"
    restart()
    assert any(e.get("boot") == 2 for e in events()), "two restarts back is gone"


def test_settings_storage_keeps_room():
    m = json.loads(api("GET", "/api/metrics")[1])["flash"]["settings"]
    assert m["used"] < m["size"] * 0.6, f"settings storage {m['used']} of {m['size']} bytes"
