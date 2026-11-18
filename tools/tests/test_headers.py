"""A browser on a shared domain sends every cookie of that domain: the board takes a few KB of them."""

import json
import urllib.error
import urllib.request

import pytest
from conftest import HOST, PASSWORD


def call(method: str, path: str, body: dict | None = None, cookie: str = "") -> tuple[int, str]:
    headers = {"X-DeckConnector": "1"} | ({"Cookie": cookie} if cookie else {})
    data = json.dumps(body).encode() if body is not None else (b"" if method != "GET" else None)
    req = urllib.request.Request(f"http://{HOST}{path}", data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.headers.get("Set-Cookie", "")
    except urllib.error.HTTPError as e:
        return e.code, ""


@pytest.mark.sim
def test_a_session_among_3_kb_of_other_cookies():
    code, set_cookie = call("POST", "/api/login", {"password": PASSWORD})
    assert code == 200, code
    session = set_cookie.split(";")[0]
    foreign = "; ".join(f"other_{i:02d}={'x' * 90}" for i in range(32))  # about 3 KB, as a parent domain sends
    try:
        for cookie in (f"{foreign}; {session}", f"{session}; {foreign}"):
            assert call("GET", "/api/status", cookie=cookie)[0] == 200, len(cookie)
        assert call("GET", "/api/status", cookie=foreign)[0] == 401
    finally:
        call("POST", "/api/logout", cookie=session)
