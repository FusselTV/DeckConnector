"""End to end tests against a real bridge on the bench. Set DECKBRIDGE_HOST to the bridge's LAN address."""

import asyncio
import atexit
import base64
import io
import ipaddress
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

import pytest
from PIL import Image

from dbtools.load import password
from dbtools.safety import NOBODY, UnsafeBridge, real_input_target, require_input_gate, settings_to_restore
from dbtools.server import Server


def _target() -> str:
    """--target sim|device. Read here, because HOST must be known when the tests import it; the environment carries
    it on to pytest-xdist workers."""
    argv = sys.argv
    for i, a in enumerate(argv):
        if a == "--target" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--target="):
            return a.split("=", 1)[1]
    return os.environ.get("DECKBRIDGE_TARGET", "device")


TARGET = os.environ["DECKBRIDGE_TARGET"] = _target()
# The firmware build embeds the React page when web/dist has one; tests of the old page's DOM skip then.
REACT_PAGE = (Path(__file__).resolve().parents[2] / "web/dist/index.html").exists()
SIM = Path(__file__).resolve().parents[2] / "firmware/sim/build/deckbridge_sim.elf"


def _start_sim() -> tuple[str, str]:
    worker = int(os.environ.get("PYTEST_XDIST_WORKER", "gw0")[2:])
    port, password = 18080 + 2 * worker, "simulator-pass"
    log = Path(f"/tmp/deckbridge-sim-{port}.log")
    sim = subprocess.Popen(
        [SIM],
        env={**os.environ, "DB_SIM_PORT": str(port), "DB_SIM_PASSWORD": password},
        stdout=log.open("w"),
        stderr=subprocess.STDOUT,
    )
    atexit.register(sim.kill)
    for _ in range(100):
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
            return f"127.0.0.1:{port}", password
        except OSError:
            if sim.poll() is not None:
                break
            time.sleep(0.05)
    raise RuntimeError(f"the simulator did not start, see {log}")


if TARGET == "sim":
    HOST, PASSWORD = _start_sim()
else:
    HOST = os.environ.get("DECKBRIDGE_HOST", "192.168.1.214")
    PASSWORD = password()
CAMERA = os.environ.get("DECKBRIDGE_CAMERA", "rtsp://192.168.1.163:8554/stream")
PORTS = (8765, 8766)


def pytest_addoption(parser):
    parser.addoption("--target", default="device", choices=("device", "sim"), help="the bridge, or firmware/sim")


def pytest_collection_modifyitems(config, items):
    if TARGET == "sim":
        for item in items:
            if not item.get_closest_marker("sim"):
                item.add_marker(pytest.mark.skip(reason="needs the device"))


def api(method: str, path: str, body: bytes | None = None, auth: bool = True, timeout: float = 30):
    real = json.loads(body).get("real") if path.startswith("/api/input") and body else None
    if real is not None:
        if not isinstance(real, str):
            raise UnsafeBridge('"real" other than a host address is for a person at the web UI: tests name the host')
        if ipaddress.ip_address(real) not in NOBODY:
            real_input_target(status())  # only a test Companion on this machine may get them
    req = urllib.request.Request(f"http://{HOST}{path}", data=body, method=method, headers={"X-DeckConnector": "1"})
    if auth:
        req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def status() -> dict:
    return json.loads(api("GET", "/api/status")[1])


signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))  # so `timeout` still runs the atexit restore


def put_config(body: dict) -> None:
    """A refused change must not go unnoticed: a refused restore leaves the owner's deck on a test controller."""
    code, reply = api("PUT", "/api/config", json.dumps(body).encode())
    if code != 200:
        raise RuntimeError(f"PUT /api/config {body} refused: {code} {reply[:200]!r}")


@pytest.fixture(autouse=True)
def input_gate():
    """Before every test: a bridge whose firmware passes simulated presses on to Companion ends the whole run.
    One still starting (a slot switch, a deploy) is waited for: the test after it may press at once."""
    try:
        current = wait_for_bridge(90)
    except TimeoutError:
        pytest.exit("the bridge does not answer, so its firmware cannot be checked before pressing", returncode=3)
    try:
        require_input_gate(current)
    except UnsafeBridge as e:
        pytest.exit(str(e), returncode=3)


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


_stream: CameraStream | None = None


def camera() -> CameraStream:
    """One running stream for the whole run: a new connection costs about 6 s, waiting for a key frame."""
    global _stream
    if _stream is None or _stream.proc.poll() is not None:
        _stream = CameraStream()
        atexit.register(_stream.close)
    return _stream


@pytest.fixture(scope="session", autouse=True)
def camera_warm():
    camera()  # its first frame arrives while the first tests run


def strip_color() -> tuple[int, int, int]:
    """The strip as the camera shows it now: the first frame from the running stream after the camera's delay."""
    _stream = camera()
    since = time.monotonic() + 0.4
    end = since + 10
    while time.monotonic() < end:
        for at, rgb in reversed(_stream.frames):
            if at > since:
                return rgb
        time.sleep(0.02)
    raise TimeoutError("no camera frame")


def jpeg(color, size=(800, 480), quality=80) -> bytes:
    buf = io.BytesIO()
    Image.new("RGB", size, color).save(buf, "JPEG", quality=quality)
    return buf.getvalue()


_loop: asyncio.AbstractEventLoop | None = None


@pytest.hookimpl(tryfirst=True)
def pytest_pyfunc_call(pyfuncitem):
    """A plain test runs in a thread while the session loop goes on: the test servers must keep answering the
    bridge's pings, or it ends their sessions after 6 s and the next tests find them gone."""
    if _loop is None or pyfuncitem.get_closest_marker("asyncio"):  # pytest-asyncio marks and wraps async tests
        return None
    args = {name: pyfuncitem.funcargs[name] for name in pyfuncitem._fixtureinfo.argnames}
    _loop.run_until_complete(asyncio.to_thread(pyfuncitem.obj, **args))
    return True


@pytest.fixture(scope="session")
async def servers():
    """Two servers on this machine; the bridge is set to use them, first one preferred."""
    global _loop
    _loop = asyncio.get_running_loop()
    # No server pings: the tests check that the bridge notices dead links by itself.
    first, second = Server(PORTS[0], "e2e-1", ping=False), Server(PORTS[1], "e2e-2", ping=False)
    await first.start()
    await second.start()
    wanted = [first.url, second.url]
    cfg = json.loads(api("GET", "/api/config")[1])
    if [u for u in cfg["servers"] if u] != wanted:
        put_config({"servers": wanted})  # applies without a restart
    # A run killed before its teardown left the test controller behind once; never keep that as the one to restore.
    back = settings_to_restore(cfg)
    restored = []

    def restore():
        if not restored:
            restored.append(True)
            put_config(back)

    atexit.register(restore)  # also on pytest.exit and on SIGTERM from `timeout`, see below
    put_config({"controller": "server", "host": ""})
    yield first, second
    await first.stop()
    await second.stop()
    restore()


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
