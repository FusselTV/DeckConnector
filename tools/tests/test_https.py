"""HTTPS on 443 next to HTTP, and the setting that leaves plain HTTP only a redirect."""

import base64
import json
import ssl
import urllib.request

import pytest
from conftest import HOST, PASSWORD, api
from websockets.asyncio.client import connect

AUTH = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode(), "X-Deckbridge": "1"}
TLS = ssl.create_default_context()
TLS.check_hostname = False
TLS.verify_mode = ssl.CERT_NONE


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


def get(url: str, headers: dict, body: bytes | None = None, method: str = "GET"):
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=TLS), NoRedirect)
    try:
        with opener.open(req, timeout=10) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


@pytest.fixture
def https_only():
    api("PUT", "/api/config", json.dumps({"https_only": True}).encode())
    yield
    get(f"https://{HOST}/api/config", AUTH, json.dumps({"https_only": False}).encode(), "PUT")


def test_the_api_and_the_page_over_https():
    code, _, body = get(f"https://{HOST}/api/status", AUTH)
    status = json.loads(body)
    assert code == 200 and len(status["https_fingerprint"]) == 95
    assert get(f"https://{HOST}/", {})[0] == 200


def test_a_login_over_https_gets_a_secure_cookie():
    body = json.dumps({"password": PASSWORD}).encode()
    _, tls_headers, _ = get(f"https://{HOST}/api/login", {"X-Deckbridge": "1"}, body, "POST")
    _, plain_headers, _ = get(f"http://{HOST}/api/login", {"X-Deckbridge": "1"}, body, "POST")
    assert "; Secure" in tls_headers["Set-Cookie"]
    assert "Secure" not in plain_headers["Set-Cookie"]


async def test_the_live_socket_over_https():
    async with connect(
        f"wss://{HOST}/api/ws", ssl=TLS, additional_headers={"Authorization": AUTH["Authorization"]}
    ) as ws:
        assert json.loads(await ws.recv())["t"] == "state"


async def test_https_only_leaves_plain_http_a_redirect(https_only):
    code, headers, _ = get(f"http://{HOST}/", {})
    assert (code, headers["Location"]) == (302, f"https://{HOST}/")
    assert get(f"http://{HOST}/api/status", AUTH)[0] == 403, "a password must not go over plain HTTP"
    with pytest.raises(Exception, match="403"):
        async with connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": AUTH["Authorization"]}):
            pass
    assert get(f"https://{HOST}/api/status", AUTH)[0] == 200
