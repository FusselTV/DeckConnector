"""Attacks from the LAN, each found in a review, each must fail."""

import json
import subprocess
import time
import urllib.request

import pytest
from conftest import HOST, PASSWORD, api, status
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
