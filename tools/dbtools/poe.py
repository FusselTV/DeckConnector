"""A real power cut: the UniFi switch turns PoE on port 4 off and on (an action, no config change).

Only with the USB serial cable unplugged, or USB keeps the board powered and nothing is cut. See
~/projects/deckbridge-bench/poe-test.md.

uv run python -m dbtools.poe --usb-unplugged
"""

import argparse
import json
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


def power_cut() -> None:
    """Power-cycle port 4, after checking that it is the PoE port the bridge hangs on."""
    switch = next(d for d in call("stat/device/")["data"] if d["_id"] == SWITCH)
    port = next(p for p in switch["port_table"] if p["port_idx"] == PORT)
    if not (port.get("port_poe") and port.get("poe_enable") and port.get("up")):
        raise SystemExit(f"port {PORT} is not a powered PoE port right now: {port.get('poe_mode')}")
    call("cmd/devmgr", {"cmd": "power-cycle", "mac": switch["mac"], "port_idx": PORT})


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--usb-unplugged", action="store_true", required=True, help="the USB serial cable is out")
    ap.parse_args()
    power_cut()
    print(f"port {PORT} power-cycled")


if __name__ == "__main__":
    main()
