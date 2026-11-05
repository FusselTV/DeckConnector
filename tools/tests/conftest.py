"""End to end tests against a real bridge on the bench. Set DECKBRIDGE_HOST to the bridge's LAN address."""

import asyncio
import base64
import io
import json
import os
import subprocess
import threading
import time
import urllib.request
from pathlib import Path

import pytest
from PIL import Image

from dbtools.server import Server

HOST = os.environ.get("DECKBRIDGE_HOST", "192.168.1.214")
CAMERA = os.environ.get("DECKBRIDGE_CAMERA", "rtsp://192.168.1.163:8554/stream")
PASSWORD = (Path.home() / ".config/deckbridge/admin_pass").read_text().strip()
PORTS = (8765, 8766)


def api(method: str, path: str, body: bytes | None = None, auth: bool = True, timeout: float = 30):
    req = urllib.request.Request(f"http://{HOST}{path}", data=body, method=method, headers={"X-Deckbridge": "1"})
    if auth:
        req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def status() -> dict:
    return json.loads(api("GET", "/api/status")[1])


def wait_for_bridge(timeout: float = 60) -> dict:
    end = time.time() + timeout
    while time.time() < end:
        try:
            return status()
        except OSError:
            time.sleep(1)
    raise TimeoutError("bridge did not come back")


def camera_frame() -> Image.Image:
    raw = subprocess.run(
        [
            "ffmpeg",
            "-loglevel",
            "error",
            "-rtsp_transport",
            "tcp",
            "-i",
            CAMERA,
            "-frames:v",
            "1",
            "-f",
            "image2",
            "-vcodec",
            "png",
            "-",
        ],
        capture_output=True,
        timeout=20,
        check=True,
    ).stdout
    return Image.open(io.BytesIO(raw)).convert("RGB")


class CameraStream:
    """The camera as a running stream: every frame's arrival time and the strip's mean colour. A single
    camera_frame() call cannot be timed, its first frame is of unknown age."""

    def __init__(self):
        x, y, x2, y2 = STRIP_IN_CAMERA
        self.proc = subprocess.Popen(
            [
                "ffmpeg",
                "-loglevel",
                "error",
                "-fflags",
                "nobuffer",
                "-flags",
                "low_delay",
                "-rtsp_transport",
                "tcp",
                "-i",
                CAMERA,
                "-vf",
                f"crop={x2 - x}:{y2 - y}:{x}:{y},scale=1:1",
                "-f",
                "rawvideo",
                "-pix_fmt",
                "rgb24",
                "-",
            ],
            stdout=subprocess.PIPE,
        )
        self.frames: list[tuple[float, tuple[int, int, int]]] = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while px := self.proc.stdout.read(3):
            self.frames.append((time.monotonic(), tuple(px)))

    def first_after(self, since: float, channel: int, timeout: float = 5) -> float | None:
        """When the first frame after `since` showed `channel` (0 red, 1 green, 2 blue) strongest."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for at, rgb in list(self.frames):
                if at > since and max(range(3), key=lambda i: rgb[i]) == channel:
                    return at
            time.sleep(0.01)
        return None

    def close(self):
        self.proc.kill()


# Where the touch strip sits in the 1920x1080 camera picture of this bench.
STRIP_IN_CAMERA = (700, 700, 1500, 790)


def strip_color() -> tuple[int, int, int]:
    crop = camera_frame().crop(STRIP_IN_CAMERA).resize((1, 1), Image.Resampling.BOX)
    return crop.getpixel((0, 0))


def jpeg(color, size=(800, 480), quality=80) -> bytes:
    buf = io.BytesIO()
    Image.new("RGB", size, color).save(buf, "JPEG", quality=quality)
    return buf.getvalue()


@pytest.fixture(scope="session")
async def servers():
    """Two servers on this machine; the bridge is set to use them, first one preferred."""
    # No server pings: the tests check that the bridge notices dead links by itself.
    first, second = Server(PORTS[0], "e2e-1", ping=False), Server(PORTS[1], "e2e-2", ping=False)
    await first.start()
    await second.start()
    wanted = [first.url, second.url]
    cfg = json.loads(api("GET", "/api/config")[1])
    if [u for u in cfg["servers"] if u] != wanted:
        api("PUT", "/api/config", json.dumps({"servers": wanted}).encode())  # applies without a restart
    if cfg["controller"] != "server" or cfg.get("host"):
        api("PUT", "/api/config", json.dumps({"controller": "server", "host": ""}).encode())
    yield first, second
    await first.stop()
    await second.stop()
    if cfg["controller"] != "server" or cfg.get("host"):
        api("PUT", "/api/config", json.dumps({"controller": cfg["controller"], "host": cfg.get("host", "")}).encode())


async def newest(server):
    """The link that connected last: tests that make the bridge connect again leave older ones behind."""
    return list(server.bridges.values())[-1] if server.bridges else await server.wait_bridge(60)


@pytest.fixture
async def bridges(servers):
    first, second = servers
    a, b = await newest(first), await newest(second)
    for _ in range(100):
        if a.device:
            break
        await asyncio.sleep(0.05)
    return a, b
