"""LAN dies, WLAN takes over. Changes the switch for a minute, so only with `pytest -m unifi`.

Port security with an empty MAC list keeps the link up and drops every frame, the hard case: the bridge does
not see a link-down event and has to notice the dead path by itself.
"""

import asyncio
import json
import subprocess
import time
from pathlib import Path

import pytest

pytestmark = pytest.mark.unifi
KEY = Path.home() / ".config/deckbridge/unifi-api-key"
SWITCH = "https://unifi.example/proxy/network/api/s/default/rest/device/000000000000000000000000"
PORT = 4
LAN_IP = "192.168.1.214"


async def answers(ip: str) -> bool:
    try:
        _, writer = await asyncio.wait_for(asyncio.open_connection(ip, 80), 1)
    except (TimeoutError, OSError):
        return False
    writer.close()
    return True


def set_port(overrides: list) -> None:
    subprocess.run(
        [
            "curl",
            "-sk",
            "-o",
            "/dev/null",
            "-H",
            f"X-API-KEY: {KEY.read_text().strip()}",
            "-X",
            "PUT",
            "-H",
            "Content-Type: application/json",
            "-d",
            json.dumps({"port_overrides": overrides}),
            SWITCH,
        ],
        check=True,
    )


async def test_dead_lan_falls_back_to_wlan(servers):
    first, _ = servers
    bridge = next(iter(first.bridges.values()), None) or await first.wait_bridge(60)
    assert bridge.ws.remote_address[0] == LAN_IP, "starts on LAN"
    set_port([{"port_idx": PORT, "port_security_enabled": True, "port_security_mac_address": []}])
    try:
        back = asyncio.ensure_future(first.wait_bridge(90))
        # the switch takes 5 to 20 s to apply the block, so the clock starts when the LAN stops answering
        while await answers(LAN_IP):
            await asyncio.sleep(0.2)
        t0 = time.monotonic()
        back = await back
        took = time.monotonic() - t0
    finally:
        set_port([])
    print(f"back over WLAN {took:.1f} s after the LAN went dead")
    assert back.ws.remote_address[0] != LAN_IP
    assert took < 12
    t1 = time.monotonic()
    home = await first.wait_bridge(60)
    print(f"back on LAN {time.monotonic() - t1:.1f} s after the port was opened again")
    assert home.ws.remote_address[0] == LAN_IP
