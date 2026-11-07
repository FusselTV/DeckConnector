"""Companion that connects as a dock host is moved to its Satellite API. A dock host on this machine (companion-check)
and a fake Satellite server on this machine's port 16622 stand in for Companion, so the bridge is never pointed at a
real one. Nothing is pressed."""

import json
import re
import socket
import struct
import subprocess
import threading
import time

import pytest
from conftest import HOST, api, put_config, status
from test_input_gate import CHECK
from test_satellite_fake import Fake, Server, wait

from dbtools.server import local_ip

KEYS = ("satellite", "satellite_pin", "controller", "host")


def logged(text: str) -> int:
    return sum(text in e["text"] for e in json.loads(api("GET", "/api/events")[1]))


def dock_greets() -> bool:
    """A refused connection is closed before the dock's first keepalive."""
    with socket.create_connection((HOST, 5343), timeout=3) as s:
        return s.recv(16) != b""


def dock_hosts(ip: str) -> list:
    return [x for x in status()["sources"] if x["host"] == ip and "via" not in x]


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
        put_config({"satellite": ip})  # a test that removed it would keep this machine refused until a restart
        put_config({k: cfg[k] for k in KEYS})
        assert wait(lambda: any(x.get("via") == "satellite" and x["connected"] for x in status()["sources"]), 30), (
            "Satellite did not connect again after the restore"
        )


class Counting(Fake):
    def handle(self):
        self.server.conns += 1
        super().handle()


@pytest.fixture
def fake():
    srv = Server(("0.0.0.0", 16622), Counting)
    srv.lines, srv.device, srv.conn, srv.conns = [], None, None, 0
    threading.Thread(target=srv.serve_forever, daemon=True).start()
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
        before = logged(moved)
        put_config({"satellite": ""})
        assert wait(lambda: json.loads(api("GET", "/api/config")[1])["satellite"] == me, 30), "satellite not saved"
        assert wait(lambda: fake.device, 15), "the fake Satellite got no ADD-DEVICE"
        assert logged(moved) == before + 1
        assert wait(lambda: not dock_hosts(me), 10), "the dock connection of this host stayed"
        assert not dock_greets(), "a dock connection from this host was let in"
        time.sleep(8)  # companion-check may connect again: it must stay refused
        assert not dock_hosts(me)
        add = next(x for x in fake.lines if x.startswith("ADD-DEVICE"))
        assert f'SERIAL="streamdeck:{serial}"' in add, f"dock serial {serial}, Satellite says {add[:120]}"
    finally:
        host.kill()


def test_a_satellite_address_set_by_hand_ends_that_hosts_dock_connection(me):
    host, _ = start_dock_host()
    try:
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        put_config({"satellite": me})  # what the web's "Auf Satellite umstellen" does
        assert wait(lambda: not dock_hosts(me), 10), "the dock connection of this host stayed"
        assert not dock_greets()
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
        before = logged(hint)
        put_config({"satellite": ""})
        assert wait(lambda: logged(hint) > before, 30), "no hint in the events"
        assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""
        assert dock_hosts(me), "the dock connection ended"
        assert dock_greets()
        time.sleep(6)
        assert logged(hint) == before + 1, "the hint was logged again"
    finally:
        host.kill()


def test_a_removed_satellite_address_is_not_set_again(me, fake):
    host, _ = start_dock_host()
    try:
        put_config({"satellite": ""})
        assert wait(lambda: json.loads(api("GET", "/api/config")[1])["satellite"] == me, 30)
        host.kill()
        put_config({"satellite": ""})  # a person removes it
        host, _ = start_dock_host()
        assert wait(lambda: any(x["kind"] == "companion" for x in dock_hosts(me)), 10)
        accepted = len([x for x in fake.lines if x.startswith("ADD-DEVICE")])
        time.sleep(15)
        assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""
        assert len([x for x in fake.lines if x.startswith("ADD-DEVICE")]) == accepted
        assert dock_hosts(me), "the dock connection ended"
    finally:
        host.kill()


def frame(op: int, payload: bytes) -> bytes:
    return bytes([0x43, 0x93, 0x8A, 0x41]) + struct.pack("<HBBII", 0, op, 0, 0, len(payload)) + payload


def test_an_elgato_app_dock_host_is_not_probed(me, fake):
    with socket.create_connection((HOST, 5343), timeout=3) as app:
        app.sendall(frame(0, b"\x03\x80" + bytes(30)))  # a plain write: what the Elgato App asks with
        assert wait(lambda: any(x["kind"] == "app" for x in dock_hosts(me)), 10)
        put_config({"satellite": ""})
        for _ in range(8):
            app.sendall(frame(0, b"\x03\x1a"))  # a keepalive answer: the bridge drops a silent host after 10 s
            time.sleep(2)
        assert dock_hosts(me), "the app host was dropped"
    assert fake.conns == 0, "the bridge probed an Elgato App host"
    assert json.loads(api("GET", "/api/config")[1])["satellite"] == ""
