"""LAN dies, WLAN takes over. Changes the switch for a minute, so only with `pytest -m unifi`.

Port security with an empty MAC list keeps the link up and drops every frame, the hard case: the bridge does
not see a link-down event and has to notice the dead path by itself.
"""

import asyncio
import base64
import json
import ssl
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import PASSWORD

pytestmark = pytest.mark.unifi
KEY = Path.home() / ".config/deckbridge/unifi-api-key"
SITE = "https://unifi.example/proxy/network/api/s/default"
SWITCH_ID = "000000000000000000000000"
PORT = 4
LAN_IP = "192.168.1.214"


async def answers(ip: str) -> bool:
    try:
        _, writer = await asyncio.wait_for(asyncio.open_connection(ip, 80), 1)
    except (TimeoutError, OSError):
        return False
    writer.close()
    return True


def unifi(method: str, path: str, body: dict | None = None) -> list:
    req = urllib.request.Request(SITE + path, data=json.dumps(body).encode() if body else None, method=method)
    req.add_header("X-API-KEY", KEY.read_text().strip())
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, context=ssl._create_unverified_context(), timeout=15) as r:
        return json.loads(r.read())["data"]


def overrides() -> list:
    switch = next(d for d in unifi("GET", "/stat/device") if d["_id"] == SWITCH_ID)
    return switch.get("port_overrides", [])


def set_port(ports: list) -> None:
    unifi("PUT", f"/rest/device/{SWITCH_ID}", {"port_overrides": ports})


def blocked(ports: list) -> list:
    """The port's other settings (STP off on port 4, for one) stay: PUT replaces the whole list."""
    own = next((o for o in ports if o["port_idx"] == PORT), {"port_idx": PORT})
    rest = [o for o in ports if o["port_idx"] != PORT]
    return [*rest, {**own, "port_security_enabled": True, "port_security_mac_address": []}]


async def test_dead_lan_falls_back_to_wlan(servers):
    first, _ = servers
    bridge = next(iter(first.bridges.values()), None) or await first.wait_bridge(60)
    assert bridge.ws.remote_address[0] == LAN_IP, "starts on LAN"
    before = overrides()
    set_port(blocked(before))
    try:
        back = asyncio.ensure_future(first.wait_bridge(90))
        # the switch takes 5 to 20 s to apply the block, so the clock starts when the LAN stops answering
        while await answers(LAN_IP):
            await asyncio.sleep(0.2)
        t0 = time.monotonic()
        back = await back
        took = time.monotonic() - t0
    finally:
        set_port(before)
    print(f"back over WLAN {took:.1f} s after the LAN went dead")
    assert back.ws.remote_address[0] != LAN_IP
    assert took < 12
    t1 = time.monotonic()
    home = await first.wait_bridge(60)
    print(f"back on LAN {time.monotonic() - t1:.1f} s after the port was opened again")
    assert home.ws.remote_address[0] == LAN_IP
    assert overrides() == before, "the switch port is back as it was"


def bridge_status(ip: str) -> dict | None:
    auth = "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    req = urllib.request.Request(f"http://{ip}/api/status", headers={"X-DeckConnector": "1", "Authorization": auth})
    try:
        with urllib.request.urlopen(req, timeout=2) as r:
            return json.loads(r.read())
    except OSError:
        return None


def app_connected(s: dict | None) -> bool:
    return bool(s) and any(x["kind"] == "app" and x["connected"] for x in s["sources"])


async def test_stream_deck_app_follows_a_dead_lan_and_comes_back():
    """The cable stays in and the LAN drops everything: the app finds the dock over WLAN, then back on LAN."""
    lan = bridge_status(LAN_IP)
    assert app_connected(lan), "the Elgato App is connected at the start"
    wlan_ip = lan["network"]["wlan"]["ip"]
    before = overrides()
    set_port(blocked(before))
    try:
        while await answers(LAN_IP):
            await asyncio.sleep(0.2)
        t0 = time.monotonic()
        gone = False
        while time.monotonic() - t0 < 45:
            s = bridge_status(wlan_ip)
            gone = gone or (s is not None and not app_connected(s))
            if gone and app_connected(s):
                break
            await asyncio.sleep(1)
        over_wlan = time.monotonic() - t0
        assert gone and app_connected(s), "the app came back over WLAN"
    finally:
        set_port(before)
    t1 = time.monotonic()
    while not (app_connected(s := bridge_status(LAN_IP)) and not s["network"]["lan"]["dead"]):
        assert time.monotonic() - t1 < 60, "the app is back once the LAN works"
        await asyncio.sleep(1)
    print(f"app over WLAN {over_wlan:.1f} s after the LAN died, back {time.monotonic() - t1:.1f} s after unblocking")
    assert overrides() == before, "the switch port is back as it was"
