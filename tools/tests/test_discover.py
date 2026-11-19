"""Companion's Surfaces > Discover: the board announces itself, and Companion's Setup (an unauthenticated
POST /api/config) points it at Companion: at once during setup, else after a tap on the deck. A fake Satellite
server on this machine stands in for Companion, so the bridge is never pointed at a real one. Nothing is pressed."""

import json
import random
import time
import urllib.error
import urllib.request

import pytest
from conftest import HOST, events, get_config, put_config, status
from test_satellite_fake import PORT, drawing, wait
from zeroconf import ServiceBrowser, Zeroconf

from dbtools.satfake import FakeSatellite
from dbtools.server import local_ip

KEYS = ("satellite", "satellite_auto", "satellite_pin", "controller", "host")


def setup_request(body: dict, kind: str = "application/json", host: str = HOST) -> tuple[int, dict]:
    time.sleep(2.1)  # the board takes one Setup per 2 s
    req = urllib.request.Request(
        f"http://{HOST}/api/config",
        data=json.dumps(body).encode(),
        method="POST",
        headers={"Content-Type": kind, "Host": host},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, {}


@pytest.fixture
def fake():
    srv = FakeSatellite(PORT)
    cfg = get_config()
    try:
        yield srv
    finally:
        put_config({k: cfg[k] for k in KEYS})
        srv.shutdown()
        if srv.conn:
            srv.conn.close()  # server_close leaves the accepted connection open
        srv.server_close()


def ours(zc: Zeroconf, kind: str):
    """The board's announcement of `kind`, None after 10 s."""
    found = []

    class Seen:
        def add_service(self, z, t, name):
            found.append(z.get_service_info(t, name, 3000))

        update_service = remove_service = lambda *a: None

    ServiceBrowser(zc, kind, Seen())
    return wait(lambda: next((i for i in found if i and HOST in i.parsed_addresses()), None), 10)


def test_companion_discover_lists_the_board_under_both_service_types():
    zc = Zeroconf()
    try:
        for kind in ("_comp-sat-app._tcp.local.", "_companion-satellite._tcp.local."):
            info = ours(zc, kind)
            assert info, f"no {kind} from {HOST}"
            assert info.port == 80 and info.properties.get(b"restEnabled") == b"true"
    finally:
        zc.close()


@pytest.mark.sim
def test_setup_during_setup_points_the_bridge_at_companion():
    if get_config()["setup_done"]:
        pytest.skip("for a bridge in setup: the simulator starts as one")
    put_config({"satellite": "wss://old.example/satellite", "satellite_pin": "ab" * 32})
    assert setup_request({"host": "192.0.2.10", "port": 16622}) == (200, {"ok": True})
    cfg = get_config()
    assert {k: cfg[k] for k in ("satellite", "satellite_pin", "controller", "host")} == {
        "satellite": "192.0.2.10:16622",
        "satellite_pin": "",
        "controller": "companion",
        "host": "satellite",
    }
    assert setup_request({"host": "192.0.2.10", "port": 16622}) == (200, {"ok": True})  # the same Companion again


def test_setup_asks_on_the_deck_once_set_up(fake):
    assert get_config()["setup_done"]
    ip = f"192.0.2.{random.randint(10, 250)}"  # a refused address stays held off for 5 min
    put_config({"satellite": f"{local_ip()}:{PORT}", "controller": "companion", "host": "satellite"})
    assert wait(lambda: fake.device and drawing(), 20)
    start = time.time()
    assert setup_request({"host": ip, "port": 16622}) == (200, {"ok": True, "confirm": "deck"})
    assert wait(lambda: status()["writer"] == "ui", 5), "the question did not cover the deck"
    assert setup_request({"host": "192.0.2.2", "port": 16622})[0] == 409, "a second Companion replaced the question"
    assert get_config()["satellite"] == f"{local_ip()}:{PORT}", "taken without a tap on the deck"
    assert any(f"{ip}:16622 asks for the deck" in e["text"] for e in events())
    assert wait(drawing, 75), "the deck stayed covered after the question ran out"
    print(f"\ncovered for {time.time() - start - 4.2:.0f} s")
    code = setup_request({"host": ip, "port": 16623})[0]
    assert code == 409, "an unanswered Companion could cover the deck again at once, on another port too"


@pytest.mark.sim
@pytest.mark.parametrize(
    "body, kind, host, code",
    [
        ({"host": "192.0.2.1", "port": 16622}, "text/plain", HOST, 415),  # what a web page sends without a preflight
        ({"host": "192.0.2.1", "port": 16622}, "application/json", "evil.example", 403),  # DNS rebinding
        ({"host": "a b/c", "port": 16622}, "application/json", HOST, 400),
        ({"host": "192.0.2.1:99", "port": 16622}, "application/json", HOST, 400),
        ({"host": "192.0.2.1", "port": 70000}, "application/json", HOST, 400),
        ({"port": 16622}, "application/json", HOST, 400),
    ],
    ids=["no-preflight", "rebinding", "bad-host", "host-with-port", "bad-port", "no-host"],  # HOST differs per worker
)
def test_setup_refuses_what_companion_does_not_send(body, kind, host, code):
    before = get_config()["satellite"]
    assert setup_request(body, kind, host)[0] == code
    assert get_config()["satellite"] == before
