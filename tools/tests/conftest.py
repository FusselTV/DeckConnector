"""End to end tests against a real bridge on the bench. Set DECKBRIDGE_HOST to the bridge's LAN address."""

import asyncio
import base64
import io
import json
import os
import subprocess
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
    if cfg["servers"] != wanted:
        api("PUT", "/api/config", json.dumps({"servers": wanted}).encode())
        await asyncio.sleep(5)
        await asyncio.to_thread(wait_for_bridge)
    if cfg["controller"] != "server":
        api("PUT", "/api/config", json.dumps({"controller": "server"}).encode())
    yield first, second
    await first.stop()
    await second.stop()


@pytest.fixture(scope="session")
async def bridges(servers):
    first, second = servers
    a = first.bridges.get(next(iter(first.bridges), None)) or await first.wait_bridge(60)
    b = second.bridges.get(next(iter(second.bridges), None)) or await second.wait_bridge(60)
    for _ in range(100):
        if a.device:
            break
        await asyncio.sleep(0.05)
    return a, b
