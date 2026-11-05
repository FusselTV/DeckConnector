"""Web UI login. The lockout test is last because it leaves the login locked for a moment."""

import base64
import http.cookiejar
import json
import time
import urllib.request

from conftest import HOST, PASSWORD, api


def opener():
    return urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def call(op, method, path, body=None, marker=True):
    headers = {"X-Deckbridge": "1"} if marker else {}
    req = urllib.request.Request(f"http://{HOST}{path}", data=body, method=method, headers=headers)
    try:
        with op.open(req, timeout=10) as r:
            return r.status, r.read(), r.headers
    except urllib.error.HTTPError as e:
        return e.code, e.read(), e.headers


def login(op, password):
    return call(op, "POST", "/api/login", json.dumps({"password": password}).encode())


def test_page_is_public_and_never_asks_the_browser():
    op = opener()
    code, body, _ = call(op, "GET", "/")
    assert code == 200 and b'id="login"' in body
    code, _, headers = call(op, "GET", "/api/status")
    assert code == 401 and "WWW-Authenticate" not in headers


def test_session_cookie():
    op = opener()
    assert login(op, PASSWORD + "x")[0] == 403
    code, _, headers = login(op, PASSWORD)
    assert code == 200
    cookie = headers["Set-Cookie"]
    assert "HttpOnly" in cookie and "SameSite=Strict" in cookie
    assert call(op, "GET", "/api/status")[0] == 200
    # a change without the marker header is what a foreign page could send
    assert call(op, "PUT", "/api/config", b'{"brightness": 80}', marker=False)[0] == 401
    assert call(op, "PUT", "/api/config", b'{"brightness": 80}')[0] == 200
    assert call(op, "POST", "/api/logout")[0] == 200
    assert call(op, "GET", "/api/status")[0] == 401


def test_basic_auth_needs_the_marker_for_changes():
    assert api("PUT", "/api/config", b'{"brightness": 80}')[0] == 200  # api() sends the marker
    token = base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    req = urllib.request.Request(f"http://{HOST}/api/config", data=b"{}", method="PUT")
    req.add_header("Authorization", "Basic " + token)
    try:
        code = urllib.request.urlopen(req, timeout=10).status
    except urllib.error.HTTPError as e:
        code = e.code
    assert code == 401


def test_wrong_passwords_lock_the_login():
    op = opener()
    codes = [login(op, "wrong")[0] for _ in range(6)]
    assert codes[:5] == [403] * 5
    assert codes[5] == 429 or login(op, "wrong")[0] == 429
    assert login(op, PASSWORD)[0] == 429  # even the right password waits out the lock
    assert api("GET", "/api/status")[0] == 401  # basic auth shares the lock, else it would allow endless guessing
    time.sleep(3)
    assert login(op, PASSWORD)[0] == 200
