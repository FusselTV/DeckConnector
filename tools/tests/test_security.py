"""Attacks from the LAN, each found in a review, each must fail."""

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
    assert (code, json.loads(body)["error"]) == (400, "ESP_ERR_INVALID_VERSION")


@pytest.mark.sim
@pytest.mark.parametrize(
    ("headers", "code"), [({}, 403), ({"X-DeckConnector": "1"}, 200), ({"X-Deckbridge": "1"}, 200)]
)
def test_a_login_needs_the_marker_header_old_or_new(headers, code):
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
@pytest.mark.xfail(
    reason="firmware: other sessions end only with the restart a new password asks for, and work until then "
    "(the simulator never restarts)",
    strict=True,
)
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
    assert urllib.request.urlopen(req, timeout=5).status == 200
    assert get(f"{base}/api/status", other) == 401
