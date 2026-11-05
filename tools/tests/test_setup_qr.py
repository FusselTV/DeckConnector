"""The setup WLAN's QR code on a key can be read from the camera picture, so a phone in front of it can too."""

import json
import time

import zxingcpp
from conftest import api, camera_frame, status


def press(key: int) -> None:
    assert api("POST", "/api/input", json.dumps({"key": key}).encode())[0] == 200
    time.sleep(1)


def test_setup_wlan_qr_code():
    if not status()["device"]:
        return
    keys = json.loads(api("GET", "/api/screen")[1])["keys"]
    cols = sum(1 for k in keys if k["y"] == keys[0]["y"])
    setup = cols + 1  # second key of the second row, in the menu and on the setup page alike
    assert api("POST", "/api/menu")[0] == 200
    time.sleep(1)
    press(setup)
    try:
        time.sleep(2)
        codes = [r.text for r in zxingcpp.read_barcodes(camera_frame())]
        name = json.loads(api("GET", "/api/config")[1])["name"]
        assert any(c.startswith(f"WIFI:T:WPA;S:{name};P:") and c.endswith(";;") for c in codes), codes
    finally:
        press(setup)  # Beenden
        api("POST", "/api/input", json.dumps({"key": cols - 1}).encode())  # and close the menu
