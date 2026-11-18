"""Soak test: everything that takes and gives back resources, over and over, while memory is watched.

Each round: API calls of every kind, a login and logout, web sockets opened and closed (one that stops
reading), Network Dock connections opened and dropped, a server drawing images, and every fifth round the deck
unplugged and plugged in over USB. After each round the bridge's memory and task count are noted. At the end
the trend per hour shows a leak: free memory that keeps falling, a largest block that keeps shrinking, tasks
that keep growing.

uv run python -m dbtools.soak --minutes 5
Sets the controller to server while it runs and back to what it was.
"""

import argparse
import asyncio
import base64
import contextlib
import json
import signal
import socket
import statistics
import sys
import time
import traceback

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

from dbtools.load import HOST, Client, basic_auth
from dbtools.safety import settings_to_restore
from dbtools.server import Server

KEYS = [(13 + 219 * (i % 4), 12 + 160 * (i // 4), 120, 120) for i in range(8)]


def jpeg(color, size=(120, 120)) -> bytes:
    import io

    from PIL import Image

    out = io.BytesIO()
    Image.new("RGB", size, color).save(out, "JPEG", quality=80)
    return out.getvalue()


def slope_per_hour(t: list[float], v: list[float]) -> float:
    return statistics.linear_regression(t, v).slope * 3600 if len(t) > 2 else 0.0


async def round_trip(host: str, auth: str, c: Client, bridge, n: int) -> None:
    for path in (
        "/api/status",
        "/api/config",
        "/api/screen",
        "/api/screen.jpg",
        "/api/metrics",
        "/api/profile",
        "/api/log",
        "/",
    ):
        await asyncio.to_thread(c.get, path)
    pw = auth  # login and logout with a cookie session
    body = json.dumps({"password": base64.b64decode(pw.split()[1]).decode().split(":", 1)[1]})
    login = await asyncio.to_thread(_post_cookie, host, body)
    if login:
        await asyncio.to_thread(_logout, host, login)
    for _ in range(3):  # listeners that read a little, then go
        async with connect(f"ws://{host}/api/ws", additional_headers={"Authorization": auth}) as ws:
            await asyncio.wait_for(ws.recv(), 5)
    lazy = socket.create_connection((host, 80))  # one that never reads: the bridge must drop it
    lazy.sendall(
        f"GET /api/ws HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
        f"Authorization: {auth}\r\n\r\n".encode()
    )
    for port in (5343, 5344):  # a dock host that comes and goes without saying anything
        with contextlib.suppress(OSError):
            socket.create_connection((host, port), timeout=3).close()
    for i in range(40):  # a server drawing
        await bridge.send_image(KEYS[i % 8], jpeg(((n * 37 + i * 11) % 255, i * 6, 120)), timeout=10)
    lazy.close()


def _post_cookie(host: str, body: str) -> str | None:
    import http.client

    conn = http.client.HTTPConnection(host, timeout=5)
    conn.request("POST", "/api/login", body=body, headers={"X-DeckConnector": "1", "Content-Type": "application/json"})
    r = conn.getresponse()
    r.read()
    return r.getheader("Set-Cookie", "").split(";")[0] or None


def _logout(host: str, cookie: str) -> None:
    import http.client

    conn = http.client.HTTPConnection(host, timeout=5)
    conn.request("POST", "/api/logout", headers={"X-DeckConnector": "1", "Cookie": cookie})
    conn.getresponse().read()


async def main_async(a) -> None:
    auth = basic_auth()
    c = Client(a.host, auth)

    cfg = json.loads(c.get("/api/config")[2])
    server = Server(8765, "soak", ping=False)
    samples = []
    failures: list[str] = []
    async with contextlib.AsyncExitStack() as undo:  # runs every step back, also when one of them fails
        undo.push_async_callback(server.stop)
        undo.callback(c.send, "POST", "/api/dock", {"pairing": 0})
        undo.callback(c.send, "PUT", "/api/config", {**settings_to_restore(cfg), "servers": cfg["servers"]})
        await server.start()
        c.send("PUT", "/api/config", {"controller": "server", "host": server.url, "servers": [server.url]})
        c.send("POST", "/api/dock", {"pairing": int(a.minutes * 60) + 60})  # its dock churn comes from a new host
        bridge = await server.wait_bridge(60)
        start = time.time()
        n = 0
        while time.time() - start < a.minutes * 60:
            n += 1
            if bridge.ws.state.name != "OPEN":
                bridge = await server.wait_bridge(60)
            try:
                await round_trip(a.host, auth, c, bridge, n)
            except (OSError, TimeoutError, ConnectionClosed) as e:
                step = traceback.extract_tb(e.__traceback__)[-1]
                where = next(
                    (f.line for f in reversed(traceback.extract_tb(e.__traceback__)) if "soak.py" in f.filename),
                    step.line,
                )
                failures.append(f"{(time.time() - start) / 60:.1f} min: {type(e).__name__} at {where}")
                print(f"round {n} failed: {type(e).__name__} at {where}", flush=True)
                c = Client(a.host, auth)
            if n % 5 == 0:
                await asyncio.to_thread(c.send, "POST", "/api/deck/reconnect")
                await asyncio.sleep(3)
            m = json.loads(c.get("/api/status")[2])["memory"]
            samples.append((time.time() - start, m))
            if n % 10 == 0:
                print(
                    f"{(time.time() - start) / 60:5.1f} min, round {n}: heap {m['heap_free'] // 1024} KB "
                    f"(largest {m['heap_largest'] // 1024} KB), psram {m['psram_free'] // 1024} KB, "
                    f"tasks {m['tasks']}",
                    flush=True,
                )
    if not samples:
        raise SystemExit(f"no round finished: {failures[:5]}")
    half = samples[len(samples) // 4 :]  # after the first quarter: caches and pools have filled
    t = [s[0] for s in half]
    print(f"\n{len(samples)} rounds in {samples[-1][0] / 60:.1f} min. Trend per hour after the first quarter:")
    print(f"  failed rounds: {len(failures)} {failures[:5]}")
    for key in ("heap_free", "heap_largest", "psram_free", "psram_largest", "tasks", "sockets"):
        v = [s[1][key] for s in half]
        unit = 1 if key in ("tasks", "sockets") else 1024
        print(
            f"  {key:14} first {v[0] / unit:10.0f}  last {v[-1] / unit:10.0f}  "
            f"trend {slope_per_hour(t, v) / unit:+9.1f}{'' if key == 'tasks' else ' KB'} per hour"
        )
    print(f"  heap_min_free  {samples[-1][1]['heap_min_free'] // 1024} KB, the lowest ever")


def main() -> None:
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))  # `timeout` still restores the settings
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--minutes", type=float, default=5)
    asyncio.run(main_async(ap.parse_args()))


if __name__ == "__main__":
    main()
