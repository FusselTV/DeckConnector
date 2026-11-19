"""Attacks from the LAN, each found in a review, each must fail."""

import base64
import http.client
import json
import subprocess
import time
import urllib.request

import pytest
from conftest import HOST, PASSWORD, TARGET, WORKER, api, start_sim, status
from cryptography.hazmat.primitives import serialization

from dbtools import dbfw


def post_raw(path: str, body: bytes, headers: dict) -> int:
    req = urllib.request.Request(f"http://{HOST}{path}", data=body, method="POST", headers=headers)
    try:
        return urllib.request.urlopen(req, timeout=5).status
    except urllib.error.HTTPError as e:
        return e.code


@pytest.mark.sim
def test_deep_json_does_not_crash_the_bridge():
    before = status()["uptime_s"]
    code = post_raw("/api/login", b"[" * 127, {"X-DeckConnector": "1"})
    assert code == 403
    assert api("PUT", "/api/tls", b"[" * 3000 + b"]" * 3000)[0] == 400  # the largest body that is parsed
    time.sleep(1)
    assert status()["uptime_s"] >= before, "the bridge restarted"


@pytest.mark.sim
def test_login_needs_the_marker_header():
    """A page on another site cannot add it, so it cannot guess passwords through a browser on the LAN."""
    assert post_raw("/api/login", json.dumps({"password": PASSWORD}).encode(), {}) == 403


@pytest.mark.sim
def test_a_long_origin_is_no_way_around_the_origin_check():
    origin = "http://" + "a" * 100 + ".example"
    out = subprocess.run(
        [
            "curl",
            "-s",
            "-o",
            "/dev/null",
            "-w",
            "%{http_code}",
            "-u",
            f"admin:{PASSWORD}",
            "-H",
            "Connection: Upgrade",
            "-H",
            "Upgrade: websocket",
            "-H",
            "Sec-WebSocket-Version: 13",
            "-H",
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==",
            "-H",
            f"Origin: {origin}",
            f"http://{HOST}/api/ws",
        ],
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout
    assert out == "403"


def test_the_password_is_not_in_the_log():
    assert PASSWORD not in api("GET", "/api/log")[1].decode(errors="replace")


def test_a_signed_file_below_the_security_level_is_refused():
    """Refused at the header, before anything is written."""
    key = serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None)
    code, body = api("POST", "/api/ota", dbfw.build("p4", bytes(4096), "old", key, level=0))
    assert (code, json.loads(body)["code"]) == (400, "ota_too_old")


@pytest.mark.sim
@pytest.mark.parametrize(
    ("headers", "code"), [({}, 403), ({"X-DeckConnector": "1"}, 200), ({"X-Deckbridge": "1"}, 403)]
)
def test_a_login_needs_the_marker_header(headers, code):
    body = json.dumps({"password": PASSWORD}).encode()
    assert post_raw("/api/login", body, {**headers, "Content-Type": "application/json"}) == code


def get(url: str, cookie: str = "") -> int:
    req = urllib.request.Request(url, headers={"Cookie": cookie} if cookie else {})
    try:
        return urllib.request.urlopen(req, timeout=5).status
    except urllib.error.HTTPError as e:
        return e.code


@pytest.fixture
def own_sim(request):
    """A simulator of this test's own, so what it changes cannot touch the other tests."""
    port = 28180 + WORKER
    sim = start_sim(port, DB_SIM_PASSWORD=PASSWORD, **getattr(request, "param", {}))
    yield port
    sim.kill()
    sim.wait()


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="the setup WLAN of the simulator")
@pytest.mark.parametrize("own_sim", [{"DB_SIM_VIA_AP": "1"}], indirect=True)
def test_only_requests_to_the_setup_wlan_address_skip_the_password(own_sim):
    assert get(f"http://127.0.0.1:{own_sim}/api/config") == 200, "the AP address (here 127.0.0.1) lets a phone in"
    assert get(f"http://127.0.0.2:{own_sim}/api/config") == 401, "another address of the bridge does not"


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="changes the password")
def test_a_new_password_ends_the_other_sessions(own_sim):
    base = f"http://127.0.0.1:{own_sim}"

    def login() -> str:
        req = urllib.request.Request(
            f"{base}/api/login", data=json.dumps({"password": PASSWORD}).encode(), headers={"X-DeckConnector": "1"}
        )
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.headers["Set-Cookie"].split(";")[0]

    other, mine = login(), login()
    req = urllib.request.Request(
        f"{base}/api/config",
        data=b'{"admin_pass": "another-pass"}',
        method="PUT",
        headers={"X-DeckConnector": "1", "Cookie": mine},
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        assert r.status == 200
        renewed = r.headers["Set-Cookie"].split(";")[0]
    assert get(f"{base}/api/status", other) == 401
    assert get(f"{base}/api/status", mine) == 401
    assert get(f"{base}/api/status", renewed) == 200, "the browser that set the password stays logged in"


def claim(port: int, host: str) -> tuple[int, str]:
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/setup/claim",
        data=b"",
        method="POST",
        headers={"X-DeckConnector": "1", "Host": host},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.headers.get("Set-Cookie", "")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="takes the one chance of a fresh simulator")
def test_before_setup_the_first_browser_gets_in_once_and_only_on_a_local_name(own_sim):
    assert claim(own_sim, "evil.example")[0] == 403, "DNS rebinding: another site's name must not reach it"
    code, cookie = claim(own_sim, f"127.0.0.1:{own_sim}")
    assert code == 200 and cookie.startswith("dc_session="), cookie
    assert claim(own_sim, f"127.0.0.1:{own_sim}") == (409, '{"ok":false,"error":"claimed"}')


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="changes the password of a fresh simulator")
def test_a_password_set_before_setup_closes_the_first_browser_way(own_sim):
    auth = "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    body = json.dumps({"admin_pass": "chosen-in-the-flasher"}).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{own_sim}/api/config",
        data=body,
        method="PUT",
        headers={"X-DeckConnector": "1", "Authorization": auth, "Content-Type": "application/json"},
    )
    assert urllib.request.urlopen(req, timeout=5).status == 200
    # before the restart the new password causes: the answer comes from the stored flag, the password path
    # never sets the one in memory
    assert claim(own_sim, f"127.0.0.1:{own_sim}") == (409, '{"ok":false,"error":"claimed"}')


def raw(port: int, addr: str, path: str, host: str) -> tuple[int, str]:
    c = http.client.HTTPConnection(addr, port, timeout=5)
    c.request("GET", path, headers={"Host": host})
    r = c.getresponse()
    r.read()
    return r.status, r.getheader("Location") or ""


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="the setup WLAN of the simulator")
@pytest.mark.parametrize("own_sim", [{"DB_SIM_VIA_AP": "1"}], indirect=True)
def test_phones_on_the_setup_wlan_get_the_captive_portal_and_the_lan_does_not(own_sim):
    portal = (302, "http://127.0.0.1/")
    for path, host in [
        ("/hotspot-detect.html", "captive.apple.com"),  # Apple
        ("/generate_204", "connectivitycheck.gstatic.com"),  # Android
        ("/connecttest.txt", "www.msftconnecttest.com"),  # Windows
        ("/canonical.html", "detectportal.firefox.com"),  # Firefox
        ("/", "example.com"),  # an address typed by hand
    ]:
        assert raw(own_sim, "127.0.0.1", path, host) == portal, (path, host)
    assert raw(own_sim, "127.0.0.1", "/", f"127.0.0.1:{own_sim}")[0] == 200, "the portal itself"
    assert raw(own_sim, "127.0.0.2", "/generate_204", "connectivitycheck.gstatic.com")[0] == 404, "not on the LAN"
    assert raw(own_sim, "127.0.0.2", "/", "example.com")[0] == 200, "the LAN keeps its page under any name"
