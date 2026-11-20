"""A stand-in board for Home Assistant pictures: the demo's status and deck pictures from grab.mjs, no hardware.
Runs inside the Home Assistant container, whose Python has aiohttp and Pillow.
python3 board.py <dir with status.json and deck-*.jpg> <port>. GET /_press?k=1&show=1 presses key 1 and shows
deck-1.jpg; without k it only changes the picture. Like the firmware's, the picture ignores brightness and
screen off."""

import asyncio
import json
import sys
from pathlib import Path

from aiohttp import web

DIR = Path(sys.argv[1])
TOKEN = "s" * 64
ST = json.loads((DIR / "status.json").read_text())
SERIAL = ST["decks"][0]["serial"]
PICTURES = sorted(DIR.glob("deck-*.jpg"))
shown = [0]
sockets: set[web.WebSocketResponse] = set()


def frame() -> bytes:
    return PICTURES[shown[0]].read_bytes()


@web.middleware
async def auth(req: web.Request, handler):
    open_paths = ("/api/tokens", "/_press", "/api/login/request")
    if req.path.startswith(open_paths) or req.headers.get("Authorization") == f"Bearer {TOKEN}":
        return await handler(req)
    return web.json_response({"error": "login"}, status=401)


async def send(msg: dict) -> None:
    for s in list(sockets):
        try:
            await s.send_json(msg)
        except ConnectionError:
            sockets.discard(s)  # an entry reload leaves the old socket here until its ping fails


async def changed() -> None:
    await send({"t": "state", **ST})
    await send({"t": "screen", "deck": SERIAL})


async def ws(req: web.Request) -> web.WebSocketResponse:
    s = web.WebSocketResponse(heartbeat=None)
    await s.prepare(req)
    sockets.add(s)
    await s.send_json({"t": "state", **ST})
    try:
        while not s.closed:
            await asyncio.sleep(5)
            await s.send_json({"t": "ping"})
    finally:
        sockets.discard(s)
    return s


async def config(req: web.Request) -> web.Response:
    body = await req.json()
    ST.update(body)
    if "brightness" in body:
        for deck in ST["decks"]:
            deck["brightness"] = body["brightness"]
    await changed()
    return web.json_response({"ok": True, "restart": False})


async def screen_set(req: web.Request) -> web.Response:
    ST["screen_off"] = not (await req.json()).get("on", True)
    await changed()
    return web.json_response({"ok": True})


async def press(req: web.Request) -> web.Response:
    shown[0] = int(req.query.get("show", shown[0]))
    if "k" not in req.query:
        await send({"t": "screen", "deck": SERIAL})
        return web.json_response({"ok": True})
    key = {"deck": SERIAL, "index": int(req.query["k"]) - 1, "value": 0, "x": 0, "y": 0}
    await send({"t": "input", "kind": "key_down", **key})
    await send({"t": "screen", "deck": SERIAL})
    await asyncio.sleep(0.3)
    await send({"t": "input", "kind": "key_up", **key})
    return web.json_response({"ok": True})


def reply(make):
    async def handler(req: web.Request) -> web.Response:
        return make()

    return handler


app = web.Application(middlewares=[auth])
app.add_routes(
    [
        web.get("/api/status", reply(lambda: web.json_response(ST))),
        web.get("/api/ws", ws),
        web.put("/api/config", config),
        web.get("/api/screen.jpg", reply(lambda: web.Response(body=frame(), content_type="image/jpeg"))),
        web.post("/api/screen", screen_set),
        web.post("/api/tokens", reply(lambda: web.json_response({"token": TOKEN}))),
        web.get("/api/update/check", reply(lambda: web.json_response({"current": ST["firmware"], "latest": None}))),
        web.get("/api/events", reply(lambda: web.json_response([]))),
        web.get("/_press", press),
        web.route("*", "/{tail:.*}", reply(lambda: web.json_response({"ok": True}))),
    ]
)
web.run_app(app, host="0.0.0.0", port=int(sys.argv[2]), print=None)
