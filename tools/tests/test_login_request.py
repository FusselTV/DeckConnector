"""A browser asks on the deck to log in without the password; someone at the deck accepts or denies it."""

import json
import os
import re
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, SIM, TARGET, api, status

ZEROS = "0" * 32


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


def ask(host: str = HOST) -> str:
    """A new request, past the 3 s limit an earlier test may have started."""
    for _ in range(5):
        code, reply, _ = call("POST", "/api/login/request", {"who": "pytest · urllib"}, host=host)
        if code != 429:
            assert code == 200, reply
            return reply["id"]
        time.sleep(1)
    raise AssertionError("still 429 after 5 s")


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
    worker = int(os.environ.get("PYTEST_XDIST_WORKER", "gw0")[2:])
    port = 28080 + worker
    log = Path(f"/tmp/deckbridge-sim-accept-{port}.log")
    sim = subprocess.Popen(
        [SIM],
        env={**os.environ, "DB_SIM_PORT": str(port), "DB_SIM_LOGIN_ACCEPT": "1"},
        stdout=log.open("w"),
        stderr=subprocess.STDOUT,
    )
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
                break
            except OSError:
                time.sleep(0.05)
        yield f"127.0.0.1:{port}"
    finally:
        sim.kill()
        sim.wait()


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="accepting takes a person at the deck")
def test_an_accepted_request_logs_in_once(accepting_sim):
    host = accepting_sim
    assert call("GET", "/api/status", host=host)[0] == 401
    rid = ask(host)
    assert call("GET", f"/api/login/request/{rid}", host=host)[1]["state"] == "pending"
    time.sleep(1.2)
    code, reply, cookie = call("GET", f"/api/login/request/{rid}", host=host)
    assert (code, reply["state"]) == (200, "accepted") and cookie.startswith("db_session="), (reply, cookie)
    session = cookie.split(";")[0]
    assert call("GET", "/api/status", host=host, cookie=session)[0] == 200
    code, reply, cookie = call("GET", f"/api/login/request/{rid}", host=host)
    assert (reply["state"], cookie) == ("used", "")
