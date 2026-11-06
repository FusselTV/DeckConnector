"""API tokens for integrations, the route list in GET /api, CORS, and that docs/api.md lists every route."""

import json
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, api
from websockets.asyncio.client import connect

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
        if r["access"] == "admin":
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
