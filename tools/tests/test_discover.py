"""Companion's Surfaces > Discover: the board announces itself, and Companion's Setup (an unauthenticated
POST /api/config) points it at Companion. A fake Satellite server on this machine stands in for Companion, so the
bridge is never pointed at a real one. Nothing is pressed."""

import json
import threading
import time
import urllib.error
import urllib.request

import pytest
from conftest import HOST, api, get_config, put_config, status
from test_satellite_fake import PORT, Fake, Server, drawing, wait
from zeroconf import ServiceBrowser, Zeroconf

from dbtools.server import local_ip

KEYS = ("satellite", "satellite_auto", "satellite_pin", "controller", "host")


def setup_request(body: dict, kind: str = "application/json") -> tuple[int, dict]:
    time.sleep(2.1)  # the board takes one Setup per 2 s
    req = urllib.request.Request(
        f"http://{HOST}/api/config", data=json.dumps(body).encode(), method="POST", headers={"Content-Type": kind}
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, {}


@pytest.fixture
def fake():
    srv = Server(("0.0.0.0", PORT), Fake)
    srv.lines, srv.device, srv.conn = [], None, None
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    cfg = get_config()
    try:
        yield srv
    finally:
        put_config({k: cfg[k] for k in KEYS})
        srv.shutdown()
        srv.server_close()


def test_companion_discover_lists_the_board_under_both_service_types():
    names = {}
    zc = Zeroconf()
    try:
        for kind in ("_comp-sat-app._tcp.local.", "_companion-satellite._tcp.local."):
            found = []

            class Seen:
                def add_service(self, z, t, name):
                    found.append(z.get_service_info(t, name, 3000))

                update_service = remove_service = lambda *a: None

            ServiceBrowser(zc, kind, Seen())
            assert wait(lambda: any(i and HOST in i.parsed_addresses() for i in found), 10), f"no {kind} from {HOST}"
            names[kind] = next(i for i in found if i and HOST in i.parsed_addresses())
    finally:
        zc.close()
    for info in names.values():
        assert info.port == 80 and info.properties.get(b"restEnabled") == b"true"


def test_setup_points_an_idle_bridge_at_companion(fake):
    put_config({"satellite": "", "controller": "server", "host": ""})  # nobody draws, no Satellite address
    assert wait(lambda: status()["writer"] == "ui", 10)
    start = time.time()
    assert setup_request({"host": local_ip(), "port": PORT}) == (200, {"ok": True})
    assert wait(lambda: fake.device and drawing(), 10), "the fake Companion did not get the deck"
    print(f"\ndrawing {time.time() - start - 2.1:.2f} s after Setup")
    assert get_config()["satellite"] == f"{local_ip()}:{PORT}"
    assert setup_request({"host": local_ip(), "port": PORT}) == (200, {"ok": True})  # the same Companion again


def test_setup_asks_on_the_deck_while_another_companion_draws(fake):
    put_config({"satellite": f"{local_ip()}:{PORT}", "controller": "companion", "host": "satellite"})
    assert wait(lambda: fake.device and drawing(), 20)
    code, reply = setup_request({"host": "192.0.2.1", "port": 16622})
    assert (code, reply) == (200, {"ok": True, "confirm": "deck"})
    assert wait(lambda: status()["writer"] == "ui", 5), "the question did not cover the deck"
    assert get_config()["satellite"] == f"{local_ip()}:{PORT}", "taken without a tap on the deck"
    assert any("192.0.2.1:16622 asks for the deck" in e["text"] for e in json.loads(api("GET", "/api/events")[1]))
    assert wait(lambda: drawing(), 75), "the deck stayed covered after the question ran out"


@pytest.mark.parametrize(
    "body, kind, code",
    [
        ({"host": "192.0.2.1", "port": 16622}, "text/plain", 415),  # what a web page may send without a preflight
        ({"host": "a b/c", "port": 16622}, "application/json", 400),
        ({"host": "192.0.2.1", "port": 70000}, "application/json", 400),
        ({"port": 16622}, "application/json", 400),
    ],
)
def test_setup_refuses_what_companion_does_not_send(body, kind, code):
    before = get_config()["satellite"]
    assert setup_request(body, kind)[0] == code
    assert get_config()["satellite"] == before
