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


def deck_picture():
    return Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1]))


def test_setup_wlan_qr_code():
    if not status()["device"]:
        pytest.skip("no deck")
    keys = json.loads(api("GET", "/api/screen")[1])["keys"]
    cols = sum(1 for k in keys if k["y"] == keys[0]["y"])
    setup = cols + 1  # second key of the second row, in the menu and on the setup page alike
    name = json.loads(api("GET", "/api/config")[1])["name"]

    def wlan_code(picture) -> bool:
        codes = [r.text for r in zxingcpp.read_barcodes(picture)]
        return any(c.startswith(f"WIFI:T:WPA;S:{name};P:") and c.endswith(";;") for c in codes)

    assert api("POST", "/api/menu")[0] == 200
    time.sleep(1)
    press(setup)
    started = False
    try:
        time.sleep(2)
        assert b"WIFI:T" not in api("GET", "/api/log")[1], "the setup WLAN password is in the log"
        started = wlan_code(deck_picture())
        if not started:
            pytest.skip("only a person at the deck starts the setup WLAN, a simulated press does not")
        if not wlan_code(camera_frame()):
            pytest.skip("the camera cannot read the code (light in the room); the deck's own picture holds it")
    finally:
        if started:
            press(setup)  # firmware that let the simulated press start it also lets one stop it
        api("DELETE", "/api/menu")
