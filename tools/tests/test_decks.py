"""Several decks behind a hub: each has a slot, the deck routes pick one by serial, and inputs name their deck."""

import asyncio
import base64
import json
import urllib.error
import urllib.request

import pytest
import websockets
from conftest import TARGET, WORKER, start_sim

PASSWORD = "decks-pass"
AUTH = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode(), "X-DeckConnector": "1"}


@pytest.fixture(scope="module")
def host():
    """Three decks plugged in, two driven: max_decks is 2 by default."""
    port = 38080 + WORKER
    sim = start_sim(port, DB_SIM_PASSWORD=PASSWORD, DB_SIM_DECKS="3")
    yield f"127.0.0.1:{port}"
    sim.kill()
    sim.wait()


def call(host: str, method: str, path: str, body: dict | None = None):
    data = json.dumps(body).encode() if body is not None else (b"" if method != "GET" else None)
    req = urllib.request.Request(f"http://{host}{path}", data=data, method=method, headers=AUTH)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


pytestmark = [pytest.mark.sim, pytest.mark.skipif(TARGET != "sim", reason="the bench has one deck")]


def test_status_lists_the_decks_by_slot_and_the_one_over_the_limit(host):
    st = call(host, "GET", "/api/status")[1]
    assert [(d["slot"], d["serial"]) for d in st["decks"]] == [(0, "SIM0001"), (1, "SIM0002")]
    assert st["decks_over_limit"] == 1
    assert call(host, "GET", "/api/config")[1]["max_decks"] == 2


def test_max_decks_is_one_to_four(host):
    assert call(host, "PUT", "/api/config", {"max_decks": 5})[0] == 400
    assert call(host, "PUT", "/api/config", {"max_decks": 0})[0] == 400
    code, reply = call(host, "PUT", "/api/config", {"max_decks": 2})
    assert code == 200 and reply["restart"] is False


def test_deck_routes_pick_a_deck_by_serial(host):
    assert call(host, "GET", "/api/screen?deck=SIM0002")[0] == 200
    assert call(host, "GET", "/api/screen?deck=SIM0003")[0] == 404  # plugged in, but over the limit
    assert call(host, "POST", "/api/input?deck=SIM0002", {"key": 1})[0] == 200
    assert call(host, "POST", "/api/input", {"key": 1, "deck": "NOPE"})[0] == 404


def test_inputs_name_their_deck(host):
    async def run():
        async with websockets.connect(f"ws://{host}/api/ws", additional_headers=AUTH) as ws:
            await ws.send(json.dumps({"t": "input", "id": 1, "key": 3, "deck": "SIM0002"}))
            await ws.send(json.dumps({"t": "input", "id": 2, "key": 3}))
            seen = []
            while len(seen) < 4:
                msg = json.loads(await asyncio.wait_for(ws.recv(), 5))
                if msg["t"] == "input":
                    seen.append(msg["deck"])
            return seen

    assert asyncio.run(run()) == ["SIM0002", "SIM0002", "SIM0001", "SIM0001"]
    log = call(host, "GET", "/api/input/log")[1]
    assert [e["deck"] for e in log[-4:]] == ["SIM0002", "SIM0002", "SIM0001", "SIM0001"]
