"""API tokens for integrations, the route list in GET /api, CORS, and that the site API page lists every route."""

import asyncio
import base64
import json
import time
import urllib.request
from fnmatch import fnmatch
from pathlib import Path

import pytest
from conftest import HOST, PASSWORD, TARGET, api
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

pytestmark = pytest.mark.sim

DOCS = Path(__file__).resolve().parents[2] / "site/content/docs/api.mdx"


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


def test_a_token_ends_only_itself(token):
    code, body = api("POST", "/api/tokens", json.dumps({"name": "pytest-other"}).encode())
    assert code == 200, body
    other = json.loads(body)["token"]
    try:
        assert api("DELETE", "/api/token")[0] == 400  # the password names no token
        assert call("DELETE", "/api/token", token)[0] == 200
        assert call("GET", "/api/status", token)[0] == 401
        assert call("DELETE", "/api/token", token)[0] == 401
        assert call("GET", "/api/status", other)[0] == 200
    finally:
        api("DELETE", "/api/tokens/pytest-other")


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
        # browsers refuse a reply with the header twice
        assert code == 204 and h.get_all("Access-Control-Allow-Origin") == ["http://frontend.test"]
        assert "Authorization" in h["Access-Control-Allow-Headers"]
        h = call("GET", "/api/status", token, origin)[2]
        assert h.get_all("Access-Control-Allow-Origin") == ["http://frontend.test"]
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
    assert not missing, f"site/content/docs/api.mdx misses {missing}"


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
    ("DELETE", "/api/ap"),
    ("DELETE", "/api/coredump"),
    ("DELETE", "/api/frontend"),
    ("GET", "/api/boot"),
    ("GET", "/api/companion/found"),
    ("GET", "/api/config"),
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
    ("DELETE", "/api/tls"),
    ("POST", "/api/wifi/join"),
    ("GET", "/api/wifi/scan"),
    ("DELETE", "/api/pairing/denied/192.0.2.1"),
]
# a crash dump holds task stacks, where the password can linger; a TLS key a token chose could read the next login
PASSWORD_ONLY = [
    ("POST", "/api/reset"),
    ("POST", "/api/debug/crash"),
    ("PUT", "/api/tls"),
    ("DELETE", "/api/tokens/x"),
    ("GET", "/api/backup"),
    ("GET", "/api/coredump"),
    ("GET", "/api/tokens"),
    ("POST", "/api/tokens"),
]


async def ends_with_its_login(ws, end):
    """Once its login is gone, a socket gets nothing more and may not press: within one ping (5 s) it closes."""
    assert json.loads(await ws.recv())["t"] == "state"
    end()
    await ws.send(json.dumps({"t": "input", "id": 1, "key": 0}))
    with pytest.raises(ConnectionClosed):
        for _ in range(20):
            msg = json.loads(await asyncio.wait_for(ws.recv(), 10))
            assert msg["t"] in ("result", "ping") and msg.get("status", 401) == 401, msg


async def test_a_removed_token_loses_its_socket(token):
    async with connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": f"Bearer {token}"}) as ws:
        await ends_with_its_login(ws, lambda: api("DELETE", "/api/tokens/pytest"))


async def test_a_logout_ends_the_socket_of_its_session():
    code, _, headers = call(
        "POST", "/api/login", None, {"X-DeckConnector": "1"}, json.dumps({"password": PASSWORD}).encode()
    )
    cookie = {"Cookie": headers["Set-Cookie"].split(";")[0], "X-DeckConnector": "1"}
    assert code == 200
    async with connect(f"ws://{HOST}/api/ws", additional_headers=cookie) as ws:
        await ends_with_its_login(ws, lambda: call("POST", "/api/logout", None, cookie, b""))


@pytest.fixture
def full_token():
    code, body = api("POST", "/api/tokens", json.dumps({"name": "pytest-full", "right": "full"}).encode())
    assert code == 200 and json.loads(body)["right"] == "full"
    yield json.loads(body)["token"]
    api("DELETE", "/api/tokens/pytest-full")


def built(method: str, path: str) -> None:
    """Some routes exist only on the device (TLS) or in a debug build (crash)."""
    if not any(r["method"] == method and fnmatch(path, r["path"]) for r in routes()):
        pytest.skip(f"{method} {path} is not in this build")


@pytest.mark.parametrize(("method", "path"), FULL + PASSWORD_ONLY)
def test_a_control_token_may_not_use_a_full_route(token, method, path):
    built(method, path)
    assert call(method, path, token, body=b"{}" if method != "GET" else None)[0] == 403


@pytest.mark.parametrize(("method", "path"), PASSWORD_ONLY)
def test_a_full_token_may_not_manage_tokens(full_token, method, path):
    built(method, path)
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
        for _ in range(20):  # the deck makes its code with the next picture
            if call("GET", "/api/screen.jpg", token)[0] == 403:
                break
            time.sleep(0.25)
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


def test_a_good_token_is_never_locked_out(token):
    for _ in range(8):
        assert call("GET", "/api/status", "wrong")[0] == 401
    assert call("GET", "/api/status", token)[0] == 200
    login = {"X-DeckConnector": "1", "Content-Type": "application/json"}
    for _ in range(12):
        code, body, _ = call("POST", "/api/login", headers=login, body=b'{"password": "wrong"}')
        if code == 429:
            break
        assert json.loads(body)["code"] == "wrong_password"
    reply = json.loads(body)
    assert code == 429 and reply["code"] == "locked" and reply["retry"] > 0
    assert call("GET", "/api/status", token)[0] == 200, "a 256-bit token cannot be guessed, so it is never locked out"
    time.sleep(reply["retry"] + 1)
    assert api("GET", "/api/status")[0] == 200  # a good password ends the count for the next tests


@pytest.mark.sim
def test_a_backup_with_secrets_restores_them_and_needs_the_password():
    plain = json.loads(api("GET", "/api/backup")[1])
    full = json.loads(api("GET", "/api/backup?secrets=1")[1])
    assert "admin_pass" not in plain and {"admin_pass", "wifi_pass", "token"} <= full.keys()
    assert full["admin_pass"] == PASSWORD
    assert "admin_pass" in json.loads(api("GET", "/api/backup?secrets=1&_=" + "1" * 30)[1]), "a cache buster"
    assert api("PUT", "/api/config", json.dumps(full).encode())[0] == 200, "the backup goes back in as it is"


@pytest.mark.skipif(TARGET != "sim", reason="fills the settings with characters JSON must escape")
def test_a_backup_that_must_escape_much_stays_whole():
    escaped = {"host": "\x01" * 95, "satellite": "\\" * 63, "cors_origins": "\x01" * 127}  # 6 and 2 bytes each
    r = api("PUT", "/api/config", json.dumps(escaped).encode())
    assert r[0] == 200, r
    code, body = api("GET", "/api/backup?secrets=1")
    assert code == 200 and json.loads(body)["admin_pass"] == PASSWORD, body[-80:]


def test_deck_pictures_are_for_the_password_while_the_menu_shows(token):
    """The menu draws the web password, and a control token may open it: then it may not read the pictures."""
    assert call("POST", "/api/menu", token, headers={"X-DeckConnector": "1"}, body=b"")[0] == 200
    try:
        for path in ("/api/screen", "/api/screen.jpg"):
            assert call("GET", path, token)[0] == 403, path
            assert api("GET", path)[0] == 200, path
    finally:
        api("DELETE", "/api/menu")
    assert call("GET", "/api/screen.jpg", token)[0] != 403


@pytest.mark.skipif(TARGET == "sim", reason="the simulator draws no deck pictures")
def test_the_menus_pictures_answer_no_token_after_it_closed(token):
    """The menu's tiles (the web password) stay in memory after it closes; their ids must not answer a token."""
    assert api("POST", "/api/menu", b"")[0] == 200
    try:
        urls = []
        for _ in range(20):  # the menu draws with its next picture
            urls = [i["url"] for i in json.loads(api("GET", "/api/screen")[1])["images"]]
            if urls:
                break
            time.sleep(0.25)
        assert urls, "the menu drew nothing"
    finally:
        api("DELETE", "/api/menu")
    time.sleep(1)
    for url in urls:
        assert call("GET", url, token)[0] != 200, url


async def test_deck_pictures_are_for_the_password_while_the_password_shows(token):
    """The password page draws the password on the deck: a token reading the pictures would read it."""
    login = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}
    async with (
        connect(f"ws://{HOST}/api/ws", additional_headers={"Authorization": f"Bearer {token}"}) as ha,
        connect(f"ws://{HOST}/api/ws", additional_headers=login) as ui,
    ):
        await ha.recv()
        await ui.recv()
        assert call("POST", "/api/password/show", headers={"X-DeckConnector": "1"}, body=b"")[0] == 200
        try:
            for path in ("/api/screen", "/api/screen.jpg"):
                assert call("GET", path, token)[0] == 403, path
                assert api("GET", path)[0] == 200, path
            try:
                for _ in range(20):
                    msg = json.loads(await asyncio.wait_for(ha.recv(), 0.5))
                    assert not (msg["t"] == "screen" and "screen" in msg), "a token got the picture list"
            except TimeoutError:
                pass
        finally:
            api("DELETE", "/api/menu")
