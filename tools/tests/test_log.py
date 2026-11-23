"""The log over the web socket: the kept backlog first, then new lines as they happen. Only with the password."""

import asyncio
import base64
import contextlib
import json
import time
import urllib.request

import pytest
from conftest import HOST, PASSWORD, api, events, newest
from websockets.sync.client import connect


def lines(ws, until: str, secs: float = 5) -> str:
    text, end = "", time.time() + secs
    while until not in text and time.time() < end:
        msg = json.loads(ws.recv(timeout=max(0.1, end - time.time())))
        if msg["t"] == "log":
            text += msg["text"]
    return text


def test_the_log_streams_backlog_then_new_lines():
    auth = "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    with connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": auth}) as ws:
        ws.send(json.dumps({"t": "log", "id": 1}))
        assert lines(ws, "I ("), "no backlog"
        wrong = urllib.request.Request(f"http://{HOST}/api/config", headers={"Authorization": "Basic d3Jvbmc6eA=="})
        with contextlib.suppress(urllib.error.HTTPError):
            urllib.request.urlopen(wrong, timeout=5)  # one wrong password: harmless, and it logs a line
        assert "wrong web password" in lines(ws, "wrong web password"), "a new line did not arrive"


def test_a_token_gets_no_log():
    body = api("POST", "/api/tokens", json.dumps({"name": "log-test"}).encode())[1]
    token = json.loads(body)["token"]
    try:
        with connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": f"Bearer {token}"}) as ws:
            ws.send(json.dumps({"t": "log", "id": 2}))
            for _ in range(10):
                msg = json.loads(ws.recv(timeout=5))
                assert msg["t"] != "log"
                if msg["t"] == "result":
                    assert msg["status"] == 403
                    break
    finally:
        api("DELETE", "/api/tokens/log-test")


@pytest.mark.sim
def test_the_timeline_starts_with_the_boot():
    mine = [e for e in events() if e["boot"] == 0]
    assert mine[0]["kind"] == "boot" and mine[0]["code"] == "started" and mine[0]["level"] == "info"
    assert {"seq", "since_s", "at", "kind", "code", "level", "args", "text"} <= mine[0].keys()


async def test_a_server_that_connects_and_goes_is_on_the_timeline(servers):
    first, _ = servers
    bridge = await newest(first)
    since = json.loads(api("GET", "/api/status")[1])["uptime_s"]
    await bridge.ws.close()  # the bridge connects again by itself
    for _ in range(60):
        evs = events()
        news = [e["text"] for e in evs if e["kind"] == "source" and e["since_s"] >= since]
        if any(" lost" in t for t in news) and any(" connected" in t for t in news):
            break
        await asyncio.sleep(0.5)
    else:
        raise AssertionError(f"no lost and connected on the timeline: {news}")
    assert all({"since_s", "at", "kind", "code", "level", "text"} <= e.keys() for e in evs)
    assert len(evs) <= 200, "one page"
