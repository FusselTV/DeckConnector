"""Without a login, the deck shows the password or asks for a reset, but only for a request with the marker header."""

import json
import urllib.error
import urllib.request

import pytest
from conftest import HOST, TARGET, api, put_config, status


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
@pytest.mark.skipif(TARGET != "sim", reason="it would cover the owner's deck for 30 s")
def test_the_password_shows_without_a_login():
    assert call("POST", "/api/password/show") == (200, {"ok": True, "decks": 1})


@pytest.mark.sim
def test_a_reset_question_is_asked_and_taken_back_without_a_login():
    if TARGET != "sim" and not status()["device"]:
        pytest.skip("no deck")
    code, reply = call("POST", "/api/password/reset")
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
