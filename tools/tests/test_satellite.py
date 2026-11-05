"""Companion over its Satellite API. Needs a Companion the bridge can reach that is not the one in use, set as
DECKBRIDGE_SATELLITE (host:port); skipped when nothing answers there. The bridge draws from it while the test
runs, then goes back to what it had."""

import json
import os
import socket
import time

import pytest
from conftest import api, status

SATELLITE = os.environ.get("DECKBRIDGE_SATELLITE", "192.168.1.128:16622")


def reachable(addr: str) -> bool:
    host, port = addr.rsplit(":", 1)
    try:
        socket.create_connection((host, int(port)), timeout=2).close()
        return True
    except OSError:
        return False


pytestmark = pytest.mark.skipif(not reachable(SATELLITE), reason=f"no Companion at {SATELLITE}")


def put(body: dict) -> None:
    assert api("PUT", "/api/config", json.dumps(body).encode())[0] == 200


def satellite_source() -> dict | None:
    return next((x for x in status()["sources"] if x.get("via") == "satellite"), None)


def wait(cond, secs: float = 20):
    end = time.time() + secs
    while time.time() < end:
        got = cond()
        if got:
            return got
        time.sleep(0.25)
    return None


def pictures() -> dict:
    sc = json.loads(api("GET", "/api/screen")[1])
    return {(i["x"], i["y"], i["w"], i["h"]): i["url"] for i in sc["images"]}


@pytest.fixture
def drawing():
    cfg = json.loads(api("GET", "/api/config")[1])
    put({"satellite": SATELLITE, "controller": "companion", "host": "satellite"})
    try:
        assert wait(lambda: (x := satellite_source()) and x["drawing"]), "Companion did not take the deck"
        yield
    finally:
        put({"satellite": cfg["satellite"], "controller": cfg["controller"], "host": cfg["host"]})


def test_companion_draws_keys_and_strip_parts(drawing):
    rects = wait(lambda: (p := pictures()) and len(p) >= 12 and p)
    assert rects, "no full picture from Companion"
    keys = {r for r in rects if r[2:] == (120, 120)}
    strip = {r for r in rects if r[1] == 380}
    assert len(keys) == 8
    assert {r[0] for r in strip} == {0, 200, 400, 600}, "the strip comes in one part per encoder"


def test_a_swipe_turns_the_page_and_back(drawing):
    before = wait(lambda: (p := pictures()) and len(p) >= 12 and p)
    swipe = {"touch": "swipe", "x": 700, "y": 430, "x2": 100, "y2": 430}
    api("POST", "/api/input", json.dumps(swipe).encode())
    after = wait(lambda: (p := pictures()) != before and p, 5)
    assert after, "the page did not change"
    api("POST", "/api/input", json.dumps({**swipe, "x": 100, "x2": 700}).encode())
    assert wait(lambda: pictures() != after, 5), "the page did not change back"


def test_without_an_address_the_source_goes():
    cfg = json.loads(api("GET", "/api/config")[1])
    put({"satellite": SATELLITE})
    try:
        assert wait(lambda: (x := satellite_source()) and x["connected"])
        put({"satellite": ""})
        assert wait(lambda: satellite_source() is None, 10)
    finally:
        put({"satellite": cfg["satellite"]})
