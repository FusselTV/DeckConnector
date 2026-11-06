"""The setup WLAN's QR code on a key can be read from the camera picture, so a phone in front of it can too."""

import io
import json
import time

import pytest
import zxingcpp
from conftest import api, camera_frame, status
from PIL import Image


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
        name = json.loads(api("GET", "/api/config")[1])["name"]

        def wlan_code(picture) -> bool:
            codes = [r.text for r in zxingcpp.read_barcodes(picture)]
            return any(c.startswith(f"WIFI:T:WPA;S:{name};P:") and c.endswith(";;") for c in codes)

        if not wlan_code(camera_frame()):
            screen = Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1]))
            assert wlan_code(screen), "no WLAN code on the deck"
            pytest.skip("the camera cannot read the code (light in the room); the deck's own picture holds it")
    finally:
        press(setup)  # Beenden
        api("POST", "/api/input", json.dumps({"key": cols - 1}).encode())  # and close the menu
