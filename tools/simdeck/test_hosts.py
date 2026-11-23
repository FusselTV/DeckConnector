"""The hosts that draw, per model: Companion over Satellite (a fake one here, never a real one) and the Network Dock
emulation with the library inside Companion. The surface protocol is in test_surface.py."""

import base64
import json
import socket
import socketserver
import struct
import subprocess
import threading
import time
from pathlib import Path

import pytest
from conftest import ELGATO, MODELS, dismiss_prompt, drawn_since, pictures, shot

from dbtools.safety import real_input_target
from dbtools.server import local_ip

CHECK = Path(__file__).resolve().parents[1] / "companion-check/check.mjs"


class Fake(socketserver.StreamRequestHandler):
    def handle(self):
        srv = self.server
        self.wfile.write(b"BEGIN CompanionVersion=fake ApiVersion=1.16.0\nCAPS BITMAP_FORMATS=rgb ROTARY_AMOUNT=1\n")
        for raw in self.rfile:
            line = raw.decode().strip()
            srv.lines.append(line)
            if line.startswith("ADD-DEVICE"):
                dev = line.split('DEVICEID="', 1)[1].split('"', 1)[0]
                srv.devices.append(dev)
                self.wfile.write(f'ADD-DEVICE OK DEVICEID="{dev}"\n'.encode())
            elif line.startswith("PING"):
                self.wfile.write(b"PONG\n")
            elif line.startswith("PINCODE-KEY"):
                dev = line.split('DEVICEID="', 1)[1].split('"', 1)[0]
                self.wfile.write(f'PINCODE-KEY OK DEVICEID="{dev}"\n'.encode())
            srv.wfile = self.wfile


class FakeCompanion(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, port: int, host: str = "127.0.0.1"):
        super().__init__((host, port), Fake)
        self.lines, self.devices, self.wfile = [], [], None
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def send(self, line: str) -> None:
        self.wfile.write(f"{line}\n".encode())


def manifest(line: str) -> dict:
    return json.loads(base64.b64decode(line.split('LAYOUT_MANIFEST="', 1)[1].split('"', 1)[0]))


@pytest.fixture
def companion():
    srv = FakeCompanion(18995)
    yield srv
    srv.shutdown()
    srv.server_close()


@pytest.mark.parametrize("model", MODELS)
def test_satellite_adds_the_deck_and_takes_its_presses_and_pin(start, companion, model):
    sim = start(model)
    if model != "pedal":
        dismiss_prompt(sim)
    sim.config(
        satellite=f"127.0.0.1:{companion.server_address[1]}", controller="companion", host="satellite", setup_done=True
    )
    assert sim.wait(lambda: companion.devices and sim.status()["decks"][0]["writer"] == "satellite", 15), sim.log()
    deck = sim.status()["decks"][0]
    add = next(x for x in companion.lines if x.startswith("ADD-DEVICE"))
    assert f'PRODUCT_NAME="{deck["model"]}"' in add
    controls = manifest(add)["controls"]
    _, desc = sim.screen(0)
    lcds = deck["encoders"] if desc["touch"] else int(model == "neo")  # a strip part per encoder; the info bar
    assert len(controls) == deck["keys"] + deck["encoders"] + lcds, add
    dev = companion.devices[0]
    sim.tap(0, 0)
    assert sim.wait(lambda: any(x.startswith("KEY-PRESS") for x in companion.lines), 3), companion.lines[-5:]
    if model == "pedal":
        return
    companion.send(f'LOCKED-STATE DEVICEID="{dev}" LOCKED=1 CHARACTER_COUNT=0 ROTATION=0')
    assert sim.wait(lambda: sim.status()["decks"][0]["locked"], 5)
    time.sleep(0.6)
    shot(sim, 0, f"{model}-pin")
    for k in range(deck["keys"]):
        sim.tap(0, k)
        time.sleep(0.1)
    time.sleep(0.5)
    pins = [x.rsplit("KEY=", 1)[1] for x in companion.lines if x.startswith("PINCODE-KEY")]
    digits = 5 if deck["keys"] == 6 else 10  # six keys: two pages of five
    if model == "plus":
        digits = 8  # 9 and 0 are on encoders 3 and 4
    assert len(set(pins)) == digits, pins


def test_the_web_page_takes_the_pin_for_a_deck_without_a_pad(start):
    companion = FakeCompanion(18994, local_ip())  # on this machine's address, the only one real input may reach
    try:
        _pin_from_the_web(start("pedal"), companion)
    finally:
        companion.shutdown()
        companion.server_close()


def _pin_from_the_web(sim, companion) -> None:
    host, port = companion.server_address
    sim.config(satellite=f"{host}:{port}", controller="companion", host="satellite", setup_done=True)
    assert sim.wait(lambda: companion.devices and sim.status()["decks"][0]["writer"] == "satellite", 15), sim.log()
    real = real_input_target(sim.status())
    assert sim.api("POST", "/api/input", {"pin": 4, "real": real})[0] == 409, "a digit went out unlocked"
    companion.send(f'LOCKED-STATE DEVICEID="{companion.devices[0]}" LOCKED=1 CHARACTER_COUNT=0 ROTATION=0')
    assert sim.wait(lambda: sim.status()["decks"][0]["locked"], 5)
    assert sim.api("POST", "/api/input", {"pin": 4})[0] == 400
    assert sim.api("POST", "/api/input", {"pin": -2, "real": real})[0] == 400
    assert sim.api("POST", "/api/input", {"pin": 4, "real": "192.0.2.1"})[0] == 409
    assert sim.api("POST", "/api/input", {"pin": 10, "real": real})[0] == 400
    assert sim.api("POST", "/api/input", {"pin": 4, "real": real})[0] == 200

    def pins():
        return [x.rsplit("KEY=", 1)[1] for x in companion.lines if x.startswith("PINCODE-KEY")]

    assert sim.wait(lambda: pins() == ["4"], 3), pins()
    companion.send(f'LOCKED-STATE DEVICEID="{companion.devices[0]}" LOCKED=1 CHARACTER_COUNT=1 ROTATION=0')
    assert sim.wait(lambda: sim.status()["decks"][0]["pin_count"] == 1, 3)


def test_a_mix_of_decks_comes_up_in_satellite_each_with_its_layout(start, companion):
    sim = start("xl", "neo", "mirabox-293v3")
    dismiss_prompt(sim)  # one prompt, on every deck
    sim.config(
        satellite=f"127.0.0.1:{companion.server_address[1]}", controller="companion", host="satellite", setup_done=True
    )
    assert sim.wait(lambda: len(companion.devices) == 3, 15), companion.lines
    adds = [x for x in companion.lines if x.startswith("ADD-DEVICE")]
    names = sorted(x.split('PRODUCT_NAME="', 1)[1].split('"', 1)[0] for x in adds)
    assert names == ["Stream Deck Neo", "Stream Deck XL", "Stream Dock 293V3"]
    assert sorted(len(manifest(x)["controls"]) for x in adds) == [11, 15, 32]
    assert sim.wait(lambda: all(d["writer"] == "satellite" for d in sim.status()["decks"]))
    for n in range(3):
        sim.tap(n, 0)
    pressed = lambda: {x.split('"')[1] for x in companion.lines if x.startswith("KEY-PRESS")}  # noqa: E731
    assert sim.wait(lambda: len(pressed()) == 3), companion.lines


@pytest.mark.parametrize("model", [m for m in ELGATO if m != "pedal"] + ["pedal"])
def test_companions_library_drives_the_dock(start, model):
    sim = start(model)
    # what Companion's library sends through a dock and the deck_emu spec reading does not take; the bridge
    # passes it on as the host sent it, and a real + takes it (tools/companion-check on the bench)
    sim.tolerated = (
        "GET_FEATURE 0xa1",
        "GET_FEATURE 0x08",
        "GET_FEATURE 0x04",
        "SET_FEATURE 03 06 of 6 bytes",
        "SET_FEATURE 05 55 of 17 bytes",
    )
    if model != "pedal":
        dismiss_prompt(sim)
    sim.config(controller="companion", host="", setup_done=True)
    before = pictures(sim, 0) if model != "pedal" else {}
    run = subprocess.Popen(
        ["node", CHECK, "127.0.0.1:5343", "3"], cwd=CHECK.parent, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True,
    )  # fmt: skip
    time.sleep(2.5)
    sim.tap(0, 0)
    out, _ = run.communicate(timeout=30)
    assert run.returncode == 0, out
    deck = sim.status()["decks"][0]
    assert f"keys {deck['keys']}" in out or model == "neo", out
    assert '"button-down":1' in out, out
    if model != "pedal":
        assert drawn_since(sim, 0, before), "the dock host drew nothing"
        shot(sim, 0, f"{model}-dock")


def cora(op: int, payload: bytes, flags: int = 0) -> bytes:
    return struct.pack("<4sHBBII", bytes.fromhex("43938a41"), flags, op, 0, 1, len(payload)) + payload


def test_a_dock_host_that_stops_reading_does_not_hold_up_presses(start):
    sim = start("mk2")
    dismiss_prompt(sim)
    sim.config(controller="companion", host="", setup_done=True)
    dock = socket.create_connection(("127.0.0.1", 5343))
    device = socket.socket()
    device.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)  # a small window, so the bridge's buffer fills
    device.connect(("127.0.0.1", 5344))
    try:
        # Companion's questions on both ports; the device port is never read again
        dock.sendall(cora(2, b"\x03\x80"))
        device.sendall(cora(2, b"\x03\x1c"))
        assert sim.wait(lambda: sim.status()["decks"][0]["writer"] == "dock1", 5), sim.status()["decks"]
        seq = max((e["seq"] for e in json.loads(sim.api("GET", "/api/input/log")[1])), default=0)
        slowest = 0.0
        for _ in range(400):  # up to 16000 reports of 528 bytes: past what the socket buffers hold
            try:
                for sock in (dock, device):
                    sock.sendall(cora(0, b"\x03\x1a"))  # unanswered, keeps the host from timing out
            except OSError:
                break  # the bridge dropped the host that stopped reading
            since = time.monotonic()
            sim.act(*["0 key 0 1", "0 key 0 0"] * 20)  # 40 reports fit the deck's input queue
            seq += 40
            assert sim.wait(lambda n=seq - 1: json.loads(sim.api("GET", f"/api/input/log?since={n}")[1]), 10, 0.01)
            slowest = max(slowest, time.monotonic() - since)
        else:
            raise AssertionError("the stuck host kept its connection and missed presses")
        assert slowest < 1, f"the presses waited {slowest:.1f} s for the stuck host"
    finally:
        dock.close()
        device.close()


def test_a_mirabox_deck_has_no_dock(start):
    """Its dock slot answers as a Network Dock with nothing plugged in."""
    sim = start("plus", "mirabox-n4")
    sim.tolerated = ("GET_FEATURE 0xa1", "SET_FEATURE 03 06 of 6 bytes")
    sim.config(controller="companion", host="", setup_done=True)
    for port, works in ((5343, True), (5345, False)):
        run = subprocess.run(["node", CHECK, f"127.0.0.1:{port}"], cwd=CHECK.parent, capture_output=True, text=True,
                             timeout=40)  # fmt: skip
        assert (run.returncode == 0) == works, run.stdout
        assert ("device port did not open" in run.stderr) != works, run.stderr


def test_ten_keys_let_go_in_one_report_all_reach_companion(start, companion):
    sim = start("xl")
    dismiss_prompt(sim)
    sim.config(
        satellite=f"127.0.0.1:{companion.server_address[1]}", controller="companion", host="satellite", setup_done=True
    )
    assert sim.wait(lambda: companion.devices and sim.status()["decks"][0]["writer"] == "satellite", 15), sim.log()
    for k in range(10):
        sim.act(f"0 key {k} 1")
    sim.act("0 key 0 0 10")

    def count(pressed: int) -> int:
        return sum(x.startswith("KEY-PRESS") and x.endswith(f"PRESSED={pressed}") for x in companion.lines)

    assert sim.wait(lambda: count(0) == 10, 3), (count(1), count(0))
    assert count(1) == 10


def test_a_long_bitmap_id_is_cached_and_drawn_again(start, companion):
    sim = start("original")
    dismiss_prompt(sim)
    sim.config(
        satellite=f"127.0.0.1:{companion.server_address[1]}", controller="companion", host="satellite", setup_done=True
    )
    assert sim.wait(lambda: companion.devices and sim.status()["decks"][0]["writer"] == "satellite", 15), sim.log()
    dev, bid = companion.devices[0], "f" * 64
    companion.send("BITMAP-CACHE OK")
    rgb = base64.b64encode(bytes([220, 20, 20]) * 72 * 72).decode()
    first = pictures(sim, 0)
    companion.send(f'KEY-STATE DEVICEID="{dev}" CONTROLID="0/0" BITMAP="{rgb}" BITMAP_ID="{bid}"')
    assert sim.wait(lambda: any(x.startswith(f'BITMAP-COST BITMAP_ID="{bid}"') for x in companion.lines), 5)
    assert sim.wait(lambda: drawn_since(sim, 0, first), 5)
    time.sleep(0.3)
    before = pictures(sim, 0)
    companion.send(f'KEY-STATE DEVICEID="{dev}" CONTROLID="0/0" BITMAP_ID="{bid}"')
    assert sim.wait(lambda: drawn_since(sim, 0, before), 5), "the cached picture was not drawn"
    time.sleep(1)
    assert companion.devices == [dev], "the long id ended the session"
