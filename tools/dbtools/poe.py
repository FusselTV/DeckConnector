"""A real power cut: the UniFi switch turns PoE on a port (4, the bridge) off and on (an action, no config change).

Only with the USB serial cable unplugged, or USB keeps the board powered and nothing is cut. See
~/projects/deckbridge-bench/poe-test.md.

uv run python -m dbtools.poe --usb-unplugged
uv run python -m dbtools.poe --port 2      the Elgato Network Dock
"""

import argparse
import json
import os
import ssl
import urllib.request
from pathlib import Path

KEY = Path.home() / ".config/deckbridge/unifi-api-key"
SITE = "https://unifi.example/proxy/network/api/s/default"
SWITCH = "000000000000000000000000"  # USW Flex 2.5G 8 PoE, as in tests/test_lan_failover.py
PORT = 4
TLS = ssl.create_default_context()
TLS.check_hostname = False
TLS.verify_mode = ssl.CERT_NONE


def call(path: str, body: dict | None = None) -> dict:
    req = urllib.request.Request(
        f"{SITE}/{path}",
        data=json.dumps(body).encode() if body else None,
        headers={"X-API-KEY": KEY.read_text().strip(), "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, context=TLS, timeout=10) as r:
        return json.loads(r.read())


def power_cut(usb_unplugged: bool = False, port: int = PORT) -> None:
    """Power-cycle a PoE port, port 4 being the bridge's, after checking that it is powered. For the bridge, callers
    confirm the unplugged USB cable with the flag or DECKBRIDGE_USB_UNPLUGGED=1."""
    if port == PORT and not (usb_unplugged or os.environ.get("DECKBRIDGE_USB_UNPLUGGED") == "1"):
        raise SystemExit("unplug the USB serial cable first, then confirm with DECKBRIDGE_USB_UNPLUGGED=1")
    switch = next(d for d in call("stat/device/")["data"] if d["_id"] == SWITCH)
    state = next(p for p in switch["port_table"] if p["port_idx"] == port)
    if not (state.get("port_poe") and state.get("poe_enable") and state.get("up")):
        raise SystemExit(f"port {port} is not a powered PoE port right now: {state.get('poe_mode')}")
    call("cmd/devmgr", {"cmd": "power-cycle", "mac": switch["mac"], "port_idx": port})


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--usb-unplugged", action="store_true", help="the bridge's USB serial cable is out")
    ap.add_argument("--port", type=int, default=PORT, help="switch port, 2 for the Elgato Network Dock")
    a = ap.parse_args()
    power_cut(a.usb_unplugged, a.port)
    print(f"port {a.port} power-cycled")


if __name__ == "__main__":
    main()
