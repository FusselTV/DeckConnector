"""Before setup is done the deck shows a QR code that logs the phone in once (GET /?t=<token>)."""

import http.client
import io
import json
import time
from urllib.parse import urlparse

import pytest
import zxingcpp
from conftest import HOST, TARGET, api, put_config, status
from PIL import Image


def visit(path: str) -> tuple[int, str]:
    """The status and the session cookie a browser gets, without following the redirect."""
    c = http.client.HTTPConnection(HOST, timeout=10)
    c.request("GET", path)
    r = c.getresponse()
    r.read()
    cookie = (r.getheader("Set-Cookie") or "").split(";")[0]
    return r.status, cookie if cookie.startswith("db_session=") else ""


def logged_in(cookie: str) -> bool:
    """And logs out again: a session left behind takes one of the bridge's few slots."""
    c = http.client.HTTPConnection(HOST, timeout=10)
    c.request("GET", "/api/config", headers={"Cookie": cookie})
    ok = c.getresponse().status == 200
    c = http.client.HTTPConnection(HOST, timeout=10)
    c.request("POST", "/api/logout", headers={"Cookie": cookie, "X-DeckConnector": "1"})
    c.getresponse().read()
    return ok


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="the simulator's fixed code")
def test_a_login_code_works_once():
    assert visit("/?t=ffffffffffffffff") == (302, "")
    code, cookie = visit("/?t=0123456789abcdef")
    assert code == 302 and logged_in(cookie)
    assert visit("/?t=0123456789abcdef") == (302, "")


def deck_code() -> str | None:
    picture = Image.open(io.BytesIO(api("GET", "/api/screen.jpg")[1]))
    return next((r.text for r in zxingcpp.read_barcodes(picture) if "/?t=" in r.text), None)


def test_the_deck_shows_a_login_code_until_setup_is_done():
    if not status()["device"]:
        pytest.skip("no deck")
    assert deck_code() is None
    put_config({"setup_done": False})  # the deck shows the welcome page; Companion draws again below
    try:
        url = None
        for _ in range(10):
            time.sleep(1)
            if url := deck_code():
                break
        assert url, "no login code on the deck"
        assert urlparse(url).hostname == HOST.split(":")[0]
        code, cookie = visit(urlparse(url).path + "?" + urlparse(url).query)
        assert code == 302 and logged_in(cookie)
        assert visit(urlparse(url).path + "?" + urlparse(url).query) == (302, ""), "a code works only once"
        time.sleep(2)
        assert deck_code() not in (None, url), "the deck shows the next code"
        put_config({"qr_login": False})
        time.sleep(2)
        assert deck_code() is None
    finally:
        put_config({"setup_done": True, "qr_login": True})
    assert json.loads(api("GET", "/api/config")[1])["qr_login"]
