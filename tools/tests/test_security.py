"""Attacks from the LAN, each found in a review, each must fail."""

import base64
import http.client
import json
import socket
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, PASSWORD, TARGET, WORKER, api, deck_page, start_sim, status
from cryptography.hazmat.primitives import serialization
from websockets.sync.client import connect

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
    assert api("PUT", "/api/tls", b"[" * 3000 + b"]" * 3000)[0] == 400
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


def sim_login(port: int) -> str:
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/login",
        data=json.dumps({"password": PASSWORD}).encode(),
        headers={"X-DeckConnector": "1"},
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.headers["Set-Cookie"].split(";")[0]


@pytest.fixture
def own_sim(request):
    """A simulator of this test's own, so what it changes cannot touch the other tests."""
    port = 21580 + WORKER
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
    other, mine = sim_login(own_sim), sim_login(own_sim)
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


def wrong_login(port: int, addr: str, source: str = "") -> int:
    c = http.client.HTTPConnection(addr, port, timeout=5, source_address=(source, 0) if source else None)
    c.request("POST", "/api/login", body=b'{"password": "wrong"}', headers={"X-DeckConnector": "1"})
    return c.getresponse().status


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="locks out every address of its own simulator")
def test_an_ipv6_client_waits_out_the_lock_of_all_addresses(own_sim):
    """The board answers on link-local IPv6. Every IPv6 client was address 0, which counted as logged in."""
    codes = [wrong_login(own_sim, "127.0.0.1", f"127.0.0.{n}") for n in range(2, 8) for _ in range(5)]
    assert codes == [403] * 30, codes
    assert wrong_login(own_sim, "::1") == 429


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="locks out every address of its own simulator")
def test_an_ipv6_client_that_logged_in_keeps_its_own_address(own_sim):
    """It skips the lock of all addresses like an IPv4 one: it is not one shared address for every IPv6 client."""
    c = http.client.HTTPConnection("::1", own_sim, timeout=5)
    c.request("POST", "/api/login", body=json.dumps({"password": PASSWORD}).encode(), headers={"X-DeckConnector": "1"})
    assert c.getresponse().status == 200
    codes = [wrong_login(own_sim, "127.0.0.1", f"127.0.0.{n}") for n in range(2, 8) for _ in range(5)]
    assert codes == [403] * 30, codes
    assert wrong_login(own_sim, "::1") == 403


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


def put(port: int, body: dict, cookie: str = "") -> tuple[int, str]:
    auth = (
        {"Cookie": cookie}
        if cookie
        else {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}
    )
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/config",
        data=json.dumps(body).encode(),
        method="PUT",
        headers={"X-DeckConnector": "1", "Content-Type": "application/json", **auth},
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, (r.headers["Set-Cookie"] or "").split(";")[0]


CLAIMED = (409, '{"ok":false,"error":"claimed"}')


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="sets up a fresh simulator")
def test_before_setup_every_browser_gets_in_on_a_local_name_until_a_password_is_set(own_sim):
    assert claim(own_sim, "evil.example")[0] == 403, "DNS rebinding: another site's name must not reach it"
    local = f"127.0.0.1:{own_sim}"
    (a, first), (b, second) = claim(own_sim, local), claim(own_sim, local)
    assert a == b == 200 and first.startswith("dc_session=") and second.startswith("dc_session="), (first, second)
    first, second = first.split(";")[0], second.split(";")[0]
    status_url = f"http://{local}/api/status"
    assert get(status_url, first) == get(status_url, second) == 200, "the phone joins the laptop's setup"
    code, renewed = put(own_sim, {"admin_pass": "chosen-in-one-browser"}, first)
    assert code == 200 and renewed.startswith("dc_session=")
    assert get(status_url, second) == 401, "the other browser now needs that password"
    assert get(status_url, renewed) == 200
    assert claim(own_sim, local) == CLAIMED


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="sets up a fresh simulator")
def test_the_end_of_setup_ends_the_way_in_without_a_password_for_good(own_sim):
    local = f"127.0.0.1:{own_sim}"
    (code, cookie), (_, other) = claim(own_sim, local), claim(own_sim, local)
    assert code == 200
    assert put(own_sim, {"setup_done": True}, cookie.split(";")[0])[0] == 200
    assert get(f"http://{local}/api/status", other.split(";")[0]) == 401, "the other browser logs in now"
    assert claim(own_sim, local) == (409, '{"ok":false,"error":"set up"}')
    assert put(own_sim, {"setup_done": False})[0] == 200
    assert claim(own_sim, local) == CLAIMED, "setup again with the password does not open it"


CLOCK = Path(tempfile.gettempdir()) / f"deckconnector-clock-{WORKER}"


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="moves the clock of a fresh simulator")
@pytest.mark.parametrize("own_sim", [{"DB_SIM_CLOCK_FILE": str(CLOCK)}], indirect=True)
def test_the_way_in_without_a_password_ends_30_min_after_the_start(own_sim):
    local = f"127.0.0.1:{own_sim}"
    try:
        code, cookie = claim(own_sim, local)
        assert code == 200
        CLOCK.write_text("1801")
        time.sleep(0.3)  # the simulator reads the file at most every 0.1 s
        assert claim(own_sim, local) == (409, '{"ok":false,"error":"too late"}')
        assert get(f"http://{local}/api/status", cookie.split(";")[0]) == 200, "a browser already in finishes"
    finally:
        CLOCK.unlink(missing_ok=True)


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="moves the clock of a simulator of its own")
@pytest.mark.parametrize("own_sim", [{"DB_SIM_CLOCK_FILE": str(CLOCK)}], indirect=True)
def test_an_address_held_off_by_the_deck_may_ask_again_after_a_minute(own_sim):
    def ask() -> tuple[int, dict]:
        code, reply, _ = deck_page(
            "POST", "/api/login/request", b'{"who": "pytest"}', f"127.0.0.1:{own_sim}", "127.0.0.41"
        )
        return code, reply

    try:
        code, reply = ask()
        assert code == 200, reply
        call = urllib.request.Request(
            f"http://127.0.0.1:{own_sim}/api/login/request/{reply['id']}",
            method="DELETE",
            headers={"X-DeckConnector": "1"},
        )
        urllib.request.urlopen(call, timeout=5)
        assert ask()[0] == 429
        CLOCK.write_text("61")
        time.sleep(0.3)
        assert ask()[0] == 200
    finally:
        CLOCK.unlink(missing_ok=True)


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="changes the password of a fresh simulator")
def test_a_password_set_before_setup_closes_the_way_in_without_a_password(own_sim):
    auth = "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    body = json.dumps({"admin_pass": "chosen-in-the-flasher"}).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{own_sim}/api/config",
        data=body,
        method="PUT",
        headers={"X-DeckConnector": "1", "Authorization": auth, "Content-Type": "application/json"},
    )
    assert urllib.request.urlopen(req, timeout=5).status == 200
    # before the restart the new password causes
    assert claim(own_sim, f"127.0.0.1:{own_sim}") == CLAIMED


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


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="needs the simulator's fresh socket and listener order")
def test_a_socket_that_lost_its_listener_slot_may_not_change_anything(own_sim):
    """A socket off the listener list stays open until the server closes it; a frame read before that once ran
    with full rights."""
    cookie = sim_login(own_sim)
    upgrade = (
        f"GET /api/ws HTTP/1.1\r\nHost: 127.0.0.1:{own_sim}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
        f"Cookie: {sim_login(own_sim)}\r\n\r\n"
    ).encode()
    # the server reads its sockets in slot order: ours must come after the ones that push it off the list
    fillers = [socket.create_connection(("127.0.0.1", own_sim)) for _ in range(8)]
    with connect(f"ws://127.0.0.1:{own_sim}/api/ws", additional_headers={"Cookie": cookie}) as ws:
        assert json.loads(ws.recv())["t"] == "state"
        for f in fillers:
            f.close()
        time.sleep(0.3)
        others = [socket.create_connection(("127.0.0.1", own_sim)) for _ in range(8)]
        time.sleep(0.3)
        req = urllib.request.Request(
            f"http://127.0.0.1:{own_sim}/api/logout",
            data=b"",
            method="POST",
            headers={"X-DeckConnector": "1", "Cookie": cookie},
        )
        assert urllib.request.urlopen(req, timeout=5).status == 200
        for o in others:
            o.sendall(upgrade)
        ws.send(json.dumps({"t": "set", "id": 7, "brightness": 40}))
        while (frame := json.loads(ws.recv(timeout=5)))["t"] != "result":
            pass
        assert (frame["id"], frame["status"]) == (7, 401)
