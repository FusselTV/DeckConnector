"""Companion that connects as a dock host is moved to its Satellite API. A dock host on this machine (companion-check)
and a fake Satellite server on this machine's port 16622 stand in for Companion, so the bridge is never pointed at a
real one. Nothing is pressed."""

import json
import re
import socket
import struct
import subprocess
import time

import pytest
from conftest import HOST, api, get_config, put_config, status
from test_input_gate import CHECK
from test_satellite_fake import wait

from dbtools.satfake import FakeSatellite, Handler
from dbtools.server import local_ip

KEYS = ("satellite", "satellite_auto", "satellite_pin", "controller", "host")
AUTO = {"satellite": "", "satellite_auto": True, "controller": "companion", "host": ""}  # the move allowed


def logged(text: str, since: float) -> int:
    """By the bridge's clock: the event list keeps only the last 32, so a count of all of them stops growing."""
    return sum(text in e["text"] and e["at"] >= int(since) for e in json.loads(api("GET", "/api/events")[1]))


def frame(op: int, payload: bytes) -> bytes:
    return bytes([0x43, 0x93, 0x8A, 0x41]) + struct.pack("<HBBII", 0, op, 0, 0, len(payload)) + payload


def dock_takes(op: int) -> bool:
    """Whether the dock answers a first query: GET_REPORT (2) is how Companion asks, a plain write (0) the Elgato App.
    A refused host is closed once it said what it is."""
    with socket.create_connection((HOST, 5343), timeout=3) as s:
        f = s.makefile("rb")
        hdr = f.read(16)  # the greeting: a keepalive
        if len(hdr) < 16:
            return False
        f.read(struct.unpack("<I", hdr[12:16])[0])
        s.sendall(frame(op, b"\x03\x80" + (bytes(30) if op == 0 else b"")))
        try:
            return s.recv(64) != b""
        except ConnectionResetError:
            return False


def dock_hosts(ip: str) -> list:
    """Dock hosts that said what they are: a refused one is "unknown" for the moment until it is closed."""
    return [x for x in status()["sources"] if x["host"] == ip and "via" not in x and x["kind"] != "unknown"]


@pytest.fixture
def me():
    ip = local_ip()
    others = [x for x in status()["sources"] if x["kind"] == "companion" and "via" not in x and x["host"] != ip]
    if others:
        pytest.skip(f"another Companion is a dock host: the bridge would be moved to it ({others})")
    cfg = json.loads(api("GET", "/api/config")[1])
    try:
        yield ip
    finally:
        put_config({k: cfg[k] for k in KEYS})
        assert wait(lambda: any(x.get("via") == "satellite" and x["connected"] for x in status()["sources"]), 30), (
            "Satellite did not connect again after the restore"
        )


class Counting(Handler):
    def handle(self):
        self.server.conns += 1
        super().handle()


@pytest.fixture
def fake():
    srv = FakeSatellite(16622, Counting)
    srv.conns = 0
    yield srv
    srv.shutdown()
    srv.server_close()


def start_dock_host() -> tuple[subprocess.Popen, str]:
    host = subprocess.Popen(["node", "check.mjs", HOST, "90"], cwd=CHECK, stdout=subprocess.PIPE, text=True)
    serial = ""
    for line in host.stdout:
        serial = m[1] if (m := re.match(r"serial (\S+)", line)) else serial
        if line.startswith("listening"):
            return host, serial
    pytest.fail(f"companion-check ended with {host.wait()} before it listened")


def test_companion_on_the_dock_path_moves_to_satellite(me, fake):
    host, serial = start_dock_host()
    try:
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        moved = f"at {me} moved from Dock to Satellite"
        since = time.time()
        put_config(AUTO)
        assert wait(lambda: json.loads(api("GET", "/api/config")[1])["satellite"] == me, 30), "satellite not saved"
        assert wait(lambda: fake.device, 15), "the fake Satellite got no ADD-DEVICE"
        assert logged(moved, since) == 1
        assert wait(lambda: not dock_hosts(me), 10), "the dock connection of this host stayed"
        assert not dock_takes(2), "a Companion dock connection from this host was let in"
        assert dock_takes(0), "an Elgato App on the same host was refused"
        time.sleep(8)  # companion-check may connect again: it must stay refused
        assert not dock_hosts(me)
        add = next(x for x in fake.lines if x.startswith("ADD-DEVICE"))
        assert f'SERIAL="streamdeck:{serial}"' in add, f"dock serial {serial}, Satellite says {add[:120]}"
        assert 'CAN_CHANGE_PAGE="' in add, "without it Companion drops every swipe page change"
    finally:
        host.kill()


def test_a_satellite_address_set_by_hand_ends_that_hosts_dock_connection(me):
    host, _ = start_dock_host()
    try:
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        put_config({"satellite": me})  # what the web's "Satellite nutzen" does
        assert wait(lambda: not dock_hosts(me), 10), "the dock connection of this host stayed"
        assert not dock_takes(2)
        put_config({"controller": "companion", "host": me})  # a person picks the dock path for this host
        assert dock_takes(2), "a dock host picked by hand was refused"
    finally:
        host.kill()


def test_without_a_satellite_api_the_dock_stays_and_says_so(me):
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("0.0.0.0", 16622))  # nothing of ours or anyone's answers there
    host, _ = start_dock_host()
    try:
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        hint = f"Companion at {me} has no Satellite API"
        since = time.time()
        put_config(AUTO)
        assert wait(lambda: logged(hint, since), 30), "no hint in the events"
        assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""
        assert dock_hosts(me), "the dock connection ended"
        assert dock_takes(2)
        time.sleep(6)
        assert logged(hint, since) == 1, "the hint was logged again"
    finally:
        host.kill()


def test_a_removed_satellite_address_is_not_set_again(me, fake):
    host, _ = start_dock_host()
    try:
        put_config(AUTO)
        assert wait(lambda: json.loads(api("GET", "/api/config")[1])["satellite"] == me, 30)
        host.kill()
        put_config({"satellite": ""})  # a person removes it
        assert get_config()["satellite_auto"] is False
        host, _ = start_dock_host()
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        accepted = len([x for x in fake.lines if x.startswith("ADD-DEVICE")])
        time.sleep(15)
        assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""
        assert len([x for x in fake.lines if x.startswith("ADD-DEVICE")]) == accepted
        assert dock_hosts(me), "the dock connection ended"
    finally:
        host.kill()


def test_an_elgato_app_dock_host_is_not_probed(me, fake):
    with socket.create_connection((HOST, 5343), timeout=3) as app:
        app.sendall(frame(0, b"\x03\x80" + bytes(30)))  # a plain write: what the Elgato App asks with
        assert wait(lambda: any(x["kind"] == "app" for x in dock_hosts(me)), 10)
        put_config(AUTO)
        for _ in range(8):
            app.sendall(frame(0, b"\x03\x1a"))  # a keepalive answer: the bridge drops a silent host after 10 s
            time.sleep(2)
        assert dock_hosts(me), "the app host was dropped"
    assert fake.conns == 0, "the bridge probed an Elgato App host"
    assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""


@pytest.mark.sim
def test_a_removed_address_stops_the_move_until_one_is_set_again():
    cfg = get_config()
    try:
        put_config({"satellite": "192.0.2.1"})
        assert get_config()["satellite_auto"] is True
        put_config({"satellite": ""})
        assert get_config()["satellite_auto"] is False
        put_config(AUTO)
        assert get_config()["satellite_auto"] is True
    finally:
        put_config({k: cfg[k] for k in KEYS if k in cfg})


def test_a_satellite_name_refuses_its_companion_while_the_session_is_down(me):
    name = f"{me.replace('.', '-')}.sslip.io"  # public DNS that answers with the address in the name
    try:
        socket.gethostbyname(name)
    except OSError:
        pytest.skip(f"{name} does not resolve here")
    put_config({"satellite": f"{name}:16699"})  # nothing listens there
    time.sleep(6)
    assert not any(x.get("via") == "satellite" and x["connected"] for x in status()["sources"])
    assert not dock_takes(2), "Companion at the address the name stands for was let in"
    assert dock_takes(0), "an Elgato App there was refused"
