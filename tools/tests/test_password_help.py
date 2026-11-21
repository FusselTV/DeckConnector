"""Without a login, the deck shows the password or asks for a reset, but only for a request with the marker header."""

import http.client
import json
import urllib.error
import urllib.request

import pytest
from conftest import HOST, TARGET, api, deck_page, put_config, status


def call(method: str, path: str, marked: bool = True) -> tuple[int, dict]:
    headers = {"X-DeckConnector": "1"} if marked else {}
    req = urllib.request.Request(f"http://{HOST}{path}", data=b"", method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


@pytest.mark.sim
@pytest.mark.parametrize(
    "method,path", [("POST", "/api/password/show"), ("POST", "/api/password/reset"), ("DELETE", "/api/password/reset")]
)
def test_a_foreign_page_cannot_ask(method, path):
    assert call(method, path, marked=False)[0] == 403


@pytest.mark.sim
def test_the_password_shows_without_a_login():
    try:
        assert deck_page("POST", "/api/password/show")[:2] == (200, {"ok": True, "decks": 1})
    finally:
        assert api("DELETE", "/api/menu")[0] == 200  # it counts as an open menu for 30 s


def paused(code: int, reply: dict) -> bool:
    return (code, reply.get("code")) == (429, "paused") and 58 <= reply["retry"] <= 60


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="asks from addresses of its own")
def test_the_password_hidden_on_the_deck_holds_off_that_address():
    assert deck_page("POST", "/api/password/show", source="127.0.0.31")[0] == 200
    assert api("DELETE", "/api/menu")[0] == 200  # as "Hide" on the deck
    assert paused(*deck_page("POST", "/api/password/show", source="127.0.0.31")[:2])
    try:
        assert deck_page("POST", "/api/password/show", source="127.0.0.32")[0] == 200, "another address is not"
    finally:
        api("DELETE", "/api/menu")


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="asks from addresses of its own")
def test_a_reset_question_taken_back_holds_off_that_address():
    assert deck_page("POST", "/api/password/reset", source="127.0.0.33")[0] == 200
    assert deck_page("DELETE", "/api/password/reset", source="127.0.0.33")[0] == 200
    assert paused(*deck_page("POST", "/api/password/reset", source="127.0.0.33")[:2])
    assert deck_page("DELETE", "/api/password/reset", source="127.0.0.33")[0] == 200, "taking back still works"


def ca_shows(source: str) -> bool:
    addr, port = HOST.rsplit(":", 1)
    c = http.client.HTTPConnection(addr, int(port), timeout=10, source_address=(source, 0))
    c.request("GET", "/api/ca")
    r = c.getresponse()
    r.read()
    if r.status == 404:
        pytest.skip("HTTPS is not running")
    assert r.status == 200
    return status()["menu_open"]


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="asks from addresses of its own")
def test_a_ca_download_always_works_and_shows_on_the_deck_only_when_that_address_is_not_held_off():
    try:
        assert ca_shows("127.0.0.34")
        assert api("DELETE", "/api/menu")[0] == 200
        assert not ca_shows("127.0.0.34")
        assert ca_shows("127.0.0.35")
    finally:
        api("DELETE", "/api/menu")


@pytest.mark.sim
def test_a_reset_question_is_asked_and_taken_back_without_a_login():
    if TARGET != "sim" and not status()["device"]:
        pytest.skip("no deck")
    code, reply, _ = deck_page("POST", "/api/password/reset")
    try:
        assert code == 200 and reply["ok"] and 0 < reply["s"] <= 60, reply
    finally:
        assert call("DELETE", "/api/password/reset") == (200, {"ok": True})


@pytest.mark.sim
def test_the_input_lock_is_gone():
    put_config({"input_lock": True})  # an unknown key: ignored like any other
    assert "input_lock" not in json.loads(api("GET", "/api/config")[1])
    assert "input_armed_s" not in status()
    assert api("PUT", "/api/input/arm", b'{"s": 30}')[0] in (404, 405)
