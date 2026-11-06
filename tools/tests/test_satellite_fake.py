"""The Satellite client against a fake Companion on this machine: handshake, drawing, bad pictures, and the input
gate on the Satellite path. Runs without a real Companion."""

import base64
import io
import json
import socketserver
import threading
import time

import pytest
from conftest import api, put_config, status
from PIL import Image

from dbtools.safety import real_input_target
from dbtools.server import local_ip

PORT = 16700


class Fake(socketserver.StreamRequestHandler):
    def handle(self):
        srv = self.server
        srv.conn = self.connection
        self.wfile.write(b"BEGIN CompanionVersion=fake ApiVersion=1.16.0\nCAPS BITMAP_FORMATS=rgb ROTARY_AMOUNT=1\n")
        for raw in self.rfile:
            line = raw.decode().strip()
            srv.lines.append(line)
            cmd = line.split(" ", 1)[0]
            if cmd == "ADD-DEVICE":
                dev = line.split('DEVICEID="', 1)[1].split('"', 1)[0]
                srv.device = dev
                self.wfile.write(f'ADD-DEVICE OK DEVICEID="{dev}"\n'.encode())
            elif cmd == "PING":
                self.wfile.write(b"PONG\n")

    def finish(self):
        self.server.conn = None


@pytest.fixture
def fake():
    srv = socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Fake)
    srv.daemon_threads, srv.allow_reuse_address = True, True
    srv.lines, srv.device, srv.conn = [], None, None
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    cfg = json.loads(api("GET", "/api/config")[1])
    put_config({"satellite": f"{local_ip()}:{PORT}", "controller": "companion", "host": "satellite"})
    try:
        assert wait(lambda: srv.device and drawing()), "the bridge did not hand the deck to the fake Companion"
        yield srv
    finally:
        put_config({k: cfg[k] for k in ("satellite", "controller", "host")})
        srv.shutdown()
        srv.server_close()


def wait(cond, secs: float = 20):
    end = time.time() + secs
    while time.time() < end:
        if got := cond():
            return got
        time.sleep(0.25)
    return None


def drawing() -> bool:
    return any(x.get("via") == "satellite" and x["drawing"] for x in status()["sources"])


def send(srv, line: str) -> None:
    srv.conn.sendall(line.encode() + b"\n")


def key_state(srv, rgb: bytes) -> None:
    send(srv, f'KEY-STATE DEVICEID="{srv.device}" CONTROLID="0/0" BITMAP="{base64.b64encode(rgb).decode()}"')


def key0_color() -> tuple:
    img = Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1])).convert("RGB")
    return img.getpixel((73, 72))


def reddish(c) -> bool:
    return c[0] > 180 and c[1] < 80 and c[2] < 80


def test_pictures_draw_and_bad_ones_do_not_end_the_session(fake):
    key_state(fake, bytes([220, 20, 20]) * 120 * 120)
    assert wait(lambda: reddish(key0_color()), 5), "the red picture did not reach key 0"
    key_state(fake, b"\x01\x02\x03")  # far too short for a 120x120 key
    send(fake, f'KEY-STATE DEVICEID="{fake.device}" CONTROLID="0/0" BITMAP="!!not base64!!"')
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
