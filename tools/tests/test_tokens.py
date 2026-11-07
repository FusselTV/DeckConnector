"""API tokens for integrations, the route list in GET /api, CORS, and that docs/api.md lists every route."""

import asyncio
import base64
import json
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, PASSWORD, api
from websockets.asyncio.client import connect

pytestmark = pytest.mark.sim

DOCS = Path(__file__).resolve().parents[2] / "docs/api.md"


def call(method: str, path: str, token: str | None = None, headers: dict | None = None, body: bytes | None = None):
    req = urllib.request.Request(f"http://{HOST}{path}", data=body, method=method, headers=headers or {})
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read(), r.headers
    except urllib.error.HTTPError as e:
        return e.code, e.read(), e.headers


@pytest.fixture(autouse=True)
def set_up():
    """Tokens are for a set-up bridge: until then the deck shows a login code, and its pictures are password-only."""
    if not json.loads(api("GET", "/api/config")[1])["setup_done"]:
        assert api("PUT", "/api/config", b'{"setup_done": true}')[0] == 200


@pytest.fixture
def token():
    code, body = api("POST", "/api/tokens", json.dumps({"name": "pytest"}).encode())
    assert code == 200
    yield json.loads(body)["token"]
    api("DELETE", "/api/tokens/pytest")


def routes() -> list[dict]:
    return json.loads(api("GET", "/api")[1])


def test_a_token_reaches_control_routes_and_no_admin_route(token):
    names = [t["name"] for t in json.loads(api("GET", "/api/tokens")[1])]
    assert "pytest" in names and token not in api("GET", "/api/tokens")[1].decode()
    for r in routes():
        if r["access"] == "control" and r["method"] == "GET" and "*" not in r["path"] and r["path"] != "/api/ws":
            assert call("GET", r["path"], token)[0] == 200, r
            assert call("GET", r["path"])[0] == 401, r
        if r["access"] in ("full", "admin"):
            path = r["path"].replace("*", "x")
            assert call(r["method"], path, token)[0] == 403, r  # refused before the handler runs
    assert call("GET", "/api/status", "0" * 64)[0] == 401


def test_a_removed_token_is_refused(token):
    assert call("GET", "/api/status", token)[0] == 200
    assert api("DELETE", "/api/tokens/pytest")[0] == 200
    assert call("GET", "/api/status", token)[0] == 401


async def test_a_token_on_the_web_socket_may_not_change_settings(token):
    async with connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": f"Bearer {token}"}) as ws:
        assert json.loads(await ws.recv())["t"] == "state"
        await ws.send(json.dumps({"t": "set", "id": 1, "brightness": 50}))
        while (msg := json.loads(await ws.recv()))["t"] != "result":
            pass
        assert msg["status"] == 403


def test_cors_is_off_until_an_origin_is_listed(token):
    origin = {"Origin": "http://frontend.test"}
    assert call("OPTIONS", "/api/status", headers=origin)[0] == 403
    assert "Access-Control-Allow-Origin" not in call("GET", "/api/status", token, origin)[2]
    try:
        assert api("PUT", "/api/config", json.dumps({"cors_origins": "http://frontend.test"}).encode())[0] == 200
        code, _, h = call("OPTIONS", "/api/status", headers=origin)
        assert code == 204 and h["Access-Control-Allow-Origin"] == "http://frontend.test"
        assert "Authorization" in h["Access-Control-Allow-Headers"]
        assert call("GET", "/api/status", token, origin)[2]["Access-Control-Allow-Origin"] == "http://frontend.test"
        other = {"Origin": "http://other.test"}
        assert "Access-Control-Allow-Origin" not in call("GET", "/api/status", token, other)[2]
    finally:
        api("PUT", "/api/config", json.dumps({"cors_origins": ""}).encode())


def test_a_deck_that_is_not_attached_is_a_404(token):
    decks = json.loads(call("GET", "/api/status", token)[1])["decks"]
    if decks:
        assert call("GET", f"/api/screen?deck={decks[0]['serial']}", token)[0] == 200
    assert call("GET", "/api/screen?deck=NOPE", token)[0] == 404


def test_the_docs_list_every_route():
    docs = DOCS.read_text()
    missing = [f"{r['method']} {r['path']}" for r in routes() if f"`{r['method']} {r['path'].rstrip('*')}" not in docs]
    assert not missing, f"docs/api.md misses {missing}"


async def test_a_simulated_press_reaches_no_integration(token):
    """Home Assistant listens with a token and runs automations on presses: a simulated one is not for it."""
    login = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}
    async with (
        connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": f"Bearer {token}"}) as ha,
        connect(f"ws://{HOST}/api/ws", additional_headers=login) as ui,
    ):
        await ha.recv()
        await ui.recv()
        # a tool's press bound to a host is simulated too: only a person's press from the web UI is not
        for press in ({"key": 1}, {"key": 2, "real": "192.0.2.1"}):
            assert api("POST", "/api/input", json.dumps(press).encode())[0] == 200
            seen = []
            while not seen:
                msg = json.loads(await asyncio.wait_for(ui.recv(), 3))
                if msg["t"] == "input":
                    seen.append(msg)
            assert seen[0]["simulated"] is True, press
        try:
            for _ in range(20):  # bounded: a source that redraws every second keeps the socket busy
                assert json.loads(await asyncio.wait_for(ha.recv(), 1))["t"] != "input", "a token got a simulated press"
        except TimeoutError:
            pass


# Written out, not read from GET /api: a route moved down a level by mistake must fail here.
FULL = [
    ("DELETE", "/api/coredump"),
    ("DELETE", "/api/frontend"),
    ("GET", "/api/boot"),
    ("GET", "/api/companion/found"),
    ("GET", "/api/config"),
    ("GET", "/api/coredump"),
    ("GET", "/api/log"),
    ("GET", "/api/profile"),
    ("GET", "/api/slots"),
    ("GET", "/api/update/check"),
    ("POST", "/api/dock"),
    ("POST", "/api/ota"),
    ("POST", "/api/ota/inspect"),
    ("POST", "/api/ota/url"),
    ("POST", "/api/reboot"),
    ("POST", "/api/slots/1/boot"),
    ("POST", "/api/sources/probe"),
    ("PUT", "/api/config"),
    ("PUT", "/api/frontend"),
    ("PUT", "/api/setup"),
]
PASSWORD_ONLY = [("DELETE", "/api/tokens/x"), ("GET", "/api/backup"), ("GET", "/api/tokens"), ("POST", "/api/tokens")]


@pytest.fixture
def full_token():
    code, body = api("POST", "/api/tokens", json.dumps({"name": "pytest-full", "right": "full"}).encode())
    assert code == 200 and json.loads(body)["right"] == "full"
    yield json.loads(body)["token"]
    api("DELETE", "/api/tokens/pytest-full")


@pytest.mark.parametrize(("method", "path"), FULL + PASSWORD_ONLY)
def test_a_control_token_may_not_use_a_full_route(token, method, path):
    assert call(method, path, token, body=b"{}" if method != "GET" else None)[0] == 403


@pytest.mark.parametrize(("method", "path"), PASSWORD_ONLY)
def test_a_full_token_may_not_manage_tokens(full_token, method, path):
    assert call(method, path, full_token, body=b"{}" if method != "GET" else None)[0] == 403


def test_a_full_token_reads_and_changes_settings_but_not_the_password(full_token):
    for method, path in FULL:
        if method == "GET":
            assert call(method, path, full_token)[0] != 403, path
    assert call("PUT", "/api/config", full_token, body=b"{}")[0] == 200
    assert call("PUT", "/api/config", full_token, body=b'{"admin_pass":"hijacked"}')[0] == 403
    listed = {t["name"]: t["right"] for t in json.loads(api("GET", "/api/tokens")[1])}
    assert listed["pytest-full"] == "full"


@pytest.mark.parametrize(
    "body",
    [
        rb'{"\u0061dmin_pass":"hijacked"}',  # cJSON decodes the escape: the key is admin_pass
        b'{"setup_done":false}',  # reopens setup, and the deck then draws the one-time login QR
        b'{"qr_login":true}',
    ],
    ids=["escaped-key", "setup", "qr-login"],
)
def test_a_full_token_may_not_reach_the_password_or_reopen_setup(full_token, body):
    before = api("GET", "/api/config")[1]
    assert call("PUT", "/api/config", full_token, body=body)[0] == 403
    assert api("GET", "/api/config")[1] == before


def test_deck_pictures_are_for_the_password_while_the_login_code_shows(token):
    """Until setup is done the deck draws a one-time login QR: a token reading the pictures would get a session."""
    assert api("PUT", "/api/config", b'{"setup_done": false}')[0] == 200
    try:
        for path in ("/api/screen", "/api/screen.jpg"):
            assert call("GET", path, token)[0] == 403, path
            assert api("GET", path)[0] != 403, path
    finally:
        api("PUT", "/api/config", b'{"setup_done": true}')
    assert call("GET", "/api/screen.jpg", token)[0] != 403


def test_a_token_right_is_control_or_full():
    assert api("POST", "/api/tokens", json.dumps({"name": "x", "right": "admin"}).encode())[0] == 400


def test_the_same_name_replaces_its_token(token):
    second = json.loads(api("POST", "/api/tokens", json.dumps({"name": "pytest"}).encode())[1])["token"]
    assert call("GET", "/api/status", token)[0] == 401
    assert call("GET", "/api/status", second)[0] == 200
    assert [t["name"] for t in json.loads(api("GET", "/api/tokens")[1])].count("pytest") == 1


@pytest.mark.parametrize("name", ["", "a b", "x" * 24, "ä"])
def test_token_names_are_checked(name):
    assert api("POST", "/api/tokens", json.dumps({"name": name}).encode())[0] == 400


def test_at_most_eight_tokens(token):
    have = {t["name"] for t in json.loads(api("GET", "/api/tokens")[1])}
    extra = [f"t{i}" for i in range(8 - len(have))]
    try:
        for name in extra:
            assert api("POST", "/api/tokens", json.dumps({"name": name}).encode())[0] == 200
        assert api("POST", "/api/tokens", json.dumps({"name": "ninth"}).encode())[0] == 409
    finally:
        for name in extra:
            api("DELETE", f"/api/tokens/{name}")


@pytest.mark.parametrize("origin", ["http://frontend.test.evil", "http://frontend.tes", "http://evil.frontend.test"])
def test_cors_matches_whole_origins_only(origin):
    try:
        assert api("PUT", "/api/config", json.dumps({"cors_origins": "http://frontend.test"}).encode())[0] == 200
        assert call("OPTIONS", "/api/status", headers={"Origin": origin})[0] == 403
    finally:
        api("PUT", "/api/config", json.dumps({"cors_origins": ""}).encode())


def test_wrong_tokens_lock_out_with_a_wait(token):
    for _ in range(12):
        code, body, _ = call("GET", "/api/status", "wrong")
        if code == 429:
            break
    assert code == 429 and json.loads(body)["retry"] > 0
    assert call("GET", "/api/status", token)[0] == 429, "a good token must wait too, not hear 'unknown token'"
    time.sleep(json.loads(body)["retry"] + 1)
    assert call("GET", "/api/status", token)[0] == 200  # a good try ends the count for the next tests


@pytest.mark.sim
def test_a_backup_with_secrets_restores_them_and_needs_the_password():
    plain = json.loads(api("GET", "/api/backup")[1])
    full = json.loads(api("GET", "/api/backup?secrets=1")[1])
    assert "admin_pass" not in plain and {"admin_pass", "wifi_pass", "token"} <= full.keys()
    assert full["admin_pass"] == PASSWORD
    assert api("PUT", "/api/config", json.dumps(full).encode())[0] == 200, "the backup goes back in as it is"
