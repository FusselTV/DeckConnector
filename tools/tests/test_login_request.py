"""A browser asks on the deck to log in without the password; someone at the deck accepts or denies it."""

import asyncio
import json
import re
import time
import urllib.error
import urllib.request

import pytest
from conftest import HOST, TARGET, WORKER, api, deck_page, start_sim, status
from websockets.asyncio.client import connect

ZEROS = "0" * 32
WHO = json.dumps({"who": "pytest · urllib"}).encode()


def call(method: str, path: str, body: dict | None = None, marked: bool = True, host: str = HOST, cookie: str = ""):
    """(status, reply, Set-Cookie)"""
    headers = {"X-DeckConnector": "1"} if marked else {}
    if cookie:
        headers["Cookie"] = cookie
    data = json.dumps(body).encode() if body is not None else (b"" if method != "GET" else None)
    req = urllib.request.Request(f"http://{host}{path}", data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read()), r.headers.get("Set-Cookie", "")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null"), e.headers.get("Set-Cookie", "")


def ask(host: str = "", source: str = "") -> str:
    """A new request, past the 3 s limit an earlier test may have started."""
    for _ in range(5):
        code, reply, _ = deck_page("POST", "/api/login/request", WHO, host, source)
        if code != 429 or reply.get("code") == "paused":
            assert code == 200, reply
            return reply["id"]
        time.sleep(1)
    raise AssertionError("still 429 after 5 s")


def held_off(host: str = "", source: str = "") -> int:
    """The seconds a new request from `source` must wait; fails when it is not held off."""
    code, reply, headers = deck_page("POST", "/api/login/request", WHO, host, source)
    assert (code, reply.get("code")) == (429, "paused"), reply
    assert headers["Retry-After"] == str(reply["retry"])
    return reply["retry"]


@pytest.fixture
def deck():
    if TARGET != "sim" and not status()["device"]:
        pytest.skip("no deck")


@pytest.mark.sim
def test_a_foreign_page_cannot_ask():
    assert call("POST", "/api/login/request", {"who": "x"}, marked=False)[0] == 403
    assert call("DELETE", f"/api/login/request/{ZEROS}", marked=False)[0] == 403


@pytest.mark.sim
def test_an_unknown_id_is_not_found():
    assert call("GET", f"/api/login/request/{ZEROS}")[0] == 404
    assert call("GET", "/api/login/request/short")[0] == 404
    assert call("DELETE", f"/api/login/request/{ZEROS}")[0] == 404


@pytest.mark.sim
def test_the_label_is_short():
    assert call("POST", "/api/login/request", {"who": "x" * 49})[0] == 400
    assert call("POST", "/api/login/request", {"who": "a\nb"})[0] == 400


@pytest.mark.sim
def test_a_request_waits_and_is_taken_back(deck):
    rid = ask()
    try:
        assert re.fullmatch(r"[0-9a-f]{32}", rid)
        code, reply, cookie = call("GET", f"/api/login/request/{rid}", marked=False)
        assert code == 200 and reply["state"] == "pending" and 0 < reply["s"] <= 60 and not cookie
        assert status()["menu_open"] is True
    finally:
        assert call("DELETE", f"/api/login/request/{rid}")[:2] == (200, {"ok": True})
    code, reply, cookie = call("GET", f"/api/login/request/{rid}")
    assert (code, reply, cookie) == (200, {"state": "denied", "s": 0}, "")


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="asks from addresses of its own")
def test_a_request_without_a_yes_holds_off_its_address_for_a_minute():
    """Else anyone on the network could cover the deck with questions again and again."""
    rid = ask(source="127.0.0.21")
    assert api("DELETE", "/api/menu")[0] == 200  # as a "No" on the deck
    assert call("GET", f"/api/login/request/{rid}")[1]["state"] == "denied"
    assert 58 <= held_off(source="127.0.0.21") <= 60
    call("DELETE", f"/api/login/request/{ask(source='127.0.0.22')}")  # another address may still ask
    assert 58 <= held_off(source="127.0.0.22") <= 60, "taking a request back counts as no Yes too"


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="asks over IPv6")
def test_an_ipv6_client_is_held_off_and_ipv4_clients_are_not():
    v6 = f"[::1]:{HOST.rsplit(':', 1)[1]}"
    call("DELETE", f"/api/login/request/{ask(v6, '::1')}")
    held_off(v6, "::1")
    call("DELETE", f"/api/login/request/{ask(source='127.0.0.23')}")


@pytest.mark.sim
def test_a_new_request_replaces_the_older_one(deck):
    first = ask()
    try:
        assert call("POST", "/api/login/request", {"who": "too soon"})[0] == 429
        second = ask()
    finally:
        call("DELETE", f"/api/login/request/{first}")
    try:
        assert second != first
        assert call("GET", f"/api/login/request/{first}")[1]["state"] == "denied"
        assert call("GET", f"/api/login/request/{second}")[1]["state"] == "pending"
    finally:
        call("DELETE", f"/api/login/request/{second}")


@pytest.mark.sim
def test_closing_the_menu_denies_a_request(deck):
    rid = ask()
    try:
        assert api("DELETE", "/api/menu")[0] == 200
        assert call("GET", f"/api/login/request/{rid}")[1]["state"] == "denied"
    finally:
        call("DELETE", f"/api/login/request/{rid}")


@pytest.fixture
def accepting_sim():
    """A second simulator where someone at the deck accepts every request after 1 s."""
    port = 21480 + WORKER
    sim = start_sim(port, DB_SIM_LOGIN_ACCEPT="1")
    yield f"127.0.0.1:{port}"
    sim.kill()
    sim.wait()


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="accepting takes a person at the deck")
def test_an_accepted_request_logs_in_once(accepting_sim):
    host = accepting_sim
    assert call("GET", "/api/status", host=host)[0] == 401
    rid = ask(host, "127.0.0.25")
    assert call("GET", f"/api/login/request/{rid}", host=host)[1]["state"] == "pending"
    time.sleep(1.2)
    code, reply, cookie = call("GET", f"/api/login/request/{rid}", host=host)
    assert (code, reply["state"]) == (200, "accepted") and cookie.startswith("dc_session="), (reply, cookie)
    session = cookie.split(";")[0]
    assert call("GET", "/api/status", host=host, cookie=session)[0] == 200
    code, reply, cookie = call("GET", f"/api/login/request/{rid}", host=host)
    assert (reply["state"], cookie) == ("used", "")
    call("DELETE", f"/api/login/request/{ask(host, '127.0.0.25')}", host=host)  # a Yes holds nobody off


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="takes over the deck for a minute")
def test_an_unanswered_request_expires():
    rid = ask(source="127.0.0.24")
    try:
        time.sleep(61)
        code, reply, cookie = call("GET", f"/api/login/request/{rid}")
        assert (code, reply, cookie) == (200, {"state": "expired", "s": 0}, "")
    finally:
        call("DELETE", f"/api/login/request/{rid}")
    assert 56 <= held_off(source="127.0.0.24") <= 60, "no answer holds off like a No"


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="accepting takes a person at the deck")
def test_a_denied_request_stays_denied_when_the_deck_accepts_later(accepting_sim):
    host = accepting_sim
    rid = ask(host)
    assert call("DELETE", f"/api/login/request/{rid}", host=host)[0] == 200
    time.sleep(1.5)  # the simulated person accepts after 1 s
    code, reply, cookie = call("GET", f"/api/login/request/{rid}", host=host)
    assert (code, reply["state"], cookie) == (200, "denied", "")


@pytest.mark.sim
async def test_a_token_cannot_accept_a_login_request(deck):
    """A control token may press keys: the login page must take its answer only from the deck itself."""
    code, body = api("POST", "/api/tokens", json.dumps({"name": "pytest-login"}).encode())
    assert code == 200
    bearer = {"Authorization": f"Bearer {json.loads(body)['token']}"}
    keys = len(json.loads(api("GET", "/api/screen")[1])["keys"])
    rid = ask()
    try:
        for key in range(keys):
            req = urllib.request.Request(
                f"http://{HOST}/api/input", data=json.dumps({"key": key}).encode(), method="POST", headers=bearer
            )
            with urllib.request.urlopen(req, timeout=10) as r:
                assert r.status == 200
        async with connect(f"ws://{HOST}/api/ws", additional_headers=bearer) as ws:
            for key in range(keys):
                await ws.send(json.dumps({"t": "input", "id": key, "key": key}))
            results = 0
            while results < keys:
                results += json.loads(await asyncio.wait_for(ws.recv(), 5))["t"] == "result"
        await asyncio.sleep(0.5)
        code, reply, cookie = call("GET", f"/api/login/request/{rid}")
        assert (code, reply["state"], cookie) == (200, "pending", "")
    finally:
        call("DELETE", f"/api/login/request/{rid}")
        api("DELETE", "/api/tokens/pytest-login")
