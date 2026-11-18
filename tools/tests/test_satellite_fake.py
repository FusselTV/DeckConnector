"""The Satellite client against a fake Companion on this machine: handshake, drawing, bad pictures, and the input
gate on the Satellite path. Runs without a real Companion."""

import base64
import contextlib
import io
import json
import threading
import time

import pytest
from conftest import HOST, PASSWORD, api, put_config, status
from PIL import Image
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect

from dbtools.safety import real_input_target
from dbtools.satfake import FakeSatellite
from dbtools.server import local_ip

PORT = 16700


@pytest.fixture
def fake():
    srv = FakeSatellite(PORT)
    cfg = json.loads(api("GET", "/api/config")[1])
    put_config({"satellite": f"{local_ip()}:{PORT}", "controller": "companion", "host": "satellite"})
    try:
        assert wait(lambda: srv.device and drawing()), "the bridge did not hand the deck to the fake Companion"
        yield srv
    finally:
        put_config({k: cfg[k] for k in ("satellite", "controller", "host")})
        srv.shutdown()
        srv.server_close()


def wait(cond, secs: float = 20, step: float = 0.25):
    end = time.time() + secs
    while time.time() < end:
        if got := cond():
            return got
        time.sleep(step)
    return None


def drawing() -> bool:
    return any(x.get("via") == "satellite" and x["drawing"] for x in status()["sources"])


def key_state(srv, rgb: bytes) -> None:
    srv.send(f'KEY-STATE DEVICEID="{srv.device}" CONTROLID="0/0" BITMAP="{base64.b64encode(rgb).decode()}"')


def key0_color() -> tuple:
    img = Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1])).convert("RGB")
    return img.getpixel((73, 72))


def reddish(c) -> bool:
    return c[0] > 180 and c[1] < 80 and c[2] < 80


def test_pictures_draw_and_bad_ones_do_not_end_the_session(fake):
    key_state(fake, bytes([220, 20, 20]) * 120 * 120)
    assert wait(lambda: reddish(key0_color()), 5), "the red picture did not reach key 0"
    key_state(fake, b"\x01\x02\x03")  # far too short for a 120x120 key
    fake.send(f'KEY-STATE DEVICEID="{fake.device}" CONTROLID="0/0" BITMAP="!!not base64!!"')
    key_state(fake, bytes([20, 20, 220]) * 120 * 120)
    assert wait(lambda: key0_color()[2] > 180, 5), "a bad picture ended the session or stuck the deck"
    assert drawing()


def presses(srv) -> list:
    return [x for x in srv.lines if x.startswith(("KEY-PRESS", "KEY-ROTATE"))]


def test_only_presses_bound_to_this_host_reach_satellite(fake):
    for key in (0, 1, 2):
        assert api("POST", "/api/input", json.dumps({"key": key}).encode())[0] == 200
    assert api("POST", "/api/input", json.dumps({"key": 0, "real": "192.0.2.1"}).encode())[0] == 200
    time.sleep(1)
    assert presses(fake) == [], f"simulated presses reached Satellite: {presses(fake)}"
    me = real_input_target(status())
    assert api("POST", "/api/input", json.dumps({"key": 0, "real": me}).encode())[0] == 200
    assert wait(lambda: len(presses(fake)) == 2, 5), f"the real press did not arrive: {presses(fake)}"
    assert [p.rsplit("PRESSED=", 1)[1] for p in presses(fake)] == ["1", "0"]


def locked(srv, count: int, on: int = 1) -> None:
    srv.send(f'LOCKED-STATE DEVICEID="{srv.device}" LOCKED={on} CHARACTER_COUNT={count} ROTATION=0')


def pins(srv) -> list:
    return [x.rsplit("KEY=", 1)[1] for x in srv.lines if x.startswith("PINCODE-KEY")]


def key0_frame():
    img = Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1])).convert("RGB")
    return img.getpixel((15, 72))  # inside key 0's frame on the Stream Deck +


def input_seq() -> int:
    log = json.loads(api("GET", "/api/input/log")[1])
    return log[-1]["seq"] if log else 0


def logged_since(seq: int) -> list:
    return [(e["kind"], e["index"]) for e in json.loads(api("GET", f"/api/input/log?since={seq}")[1])]


@contextlib.contextmanager
def ws_inputs():
    """The input events a web socket listener gets meanwhile."""
    auth = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}
    seen = []
    with connect(f"ws://{HOST}/api/ws", additional_headers=auth) as ws:

        def read():
            with contextlib.suppress(ConnectionClosed):
                for raw in ws:
                    msg = json.loads(raw)
                    if msg["t"] == "input":
                        seen.append((msg["kind"], msg["index"]))

        threading.Thread(target=read, daemon=True).start()
        yield seen


def test_a_locked_deck_takes_the_pin_on_its_own_pad(fake):
    assert "PINCODE_LOCK=FULL" in next(x for x in fake.lines if x.startswith("ADD-DEVICE"))
    me = real_input_target(status())  # while the fake still draws
    locked(fake, 0)
    assert wait(lambda: status()["decks"][0]["locked"] and not drawing(), 5), "the deck did not show the pad"
    with ws_inputs() as seen:
        seq = input_seq()
        _typed_on_the_pad(fake, me)
        assert logged_since(seq) == [], f"the input log kept PIN digits: {logged_since(seq)}"
        assert seen == [], f"a web socket listener got PIN digits: {seen}"
        assert api("POST", "/api/input", json.dumps({"key": 1, "real": me}).encode())[0] == 200
        assert wait(lambda: len(seen) == 2, 5), f"an unlocked press did not reach the web socket: {seen}"
        assert logged_since(seq)[-2:] == [("key_down", 1), ("key_up", 1)], logged_since(seq)


def _typed_on_the_pad(fake, me) -> None:
    assert api("POST", "/api/menu")[0] == 409, "the menu opened on a locked deck"
    assert api("POST", "/api/input", json.dumps({"key": 0}).encode())[0] == 200
    time.sleep(1)
    assert pins(fake) == [], "a simulated press reached Companion"
    for key in (0, 1):
        assert api("POST", "/api/input", json.dumps({"key": key, "real": me}).encode())[0] == 200
    assert wait(lambda: pins(fake) == ["1", "2"], 5), f"digits: {pins(fake)}"
    assert presses(fake) == [], f"a locked deck sent presses: {presses(fake)}"
    locked(fake, 2)
    locked(fake, 0)
    assert wait(lambda: reddish(key0_frame()), 2, 0.05), "a wrong PIN did not flash red"
    assert wait(lambda: not reddish(key0_frame()), 3), "the red stayed"
    held = json.dumps({"key": 2, "action": "down", "real": me}).encode()
    assert api("POST", "/api/input", held)[0] == 200  # the last digit, held while Companion unlocks
    assert wait(lambda: pins(fake) == ["1", "2", "3"], 5), f"digits: {pins(fake)}"
    locked(fake, 0, 0)
    assert wait(lambda: drawing() and not status()["decks"][0]["locked"], 5), "Companion did not draw again"
    assert api("POST", "/api/input", json.dumps({"key": 2, "action": "up", "real": me}).encode())[0] == 200
    time.sleep(1)
    assert presses(fake) == [], f"the release of a PIN digit reached Companion: {presses(fake)}"
