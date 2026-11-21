"""End to end tests against a real bridge on the bench (host in the bench file, see dbtools.site) or firmware/sim."""

import asyncio
import atexit
import base64
import contextlib
import http.client
import io
import ipaddress
import itertools
import json
import os
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

import pytest
from PIL import Image

from dbtools.benchlock import bench_lock
from dbtools.load import HOST as DEVICE
from dbtools.load import password
from dbtools.safety import NOBODY, UnsafeBridge, real_input_target, require_input_gate, settings_to_restore
from dbtools.server import Server
from dbtools.site import CAMERA, need


def _target() -> str:
    """--target sim|device. Read here, because HOST must be known when the tests import it; the environment carries
    it on to pytest-xdist workers, and only to them: a DECKBRIDGE_TARGET left in a shell must not pick the target."""
    if "PYTEST_XDIST_WORKER" in os.environ:
        return os.environ["DECKBRIDGE_TARGET"]
    argv = sys.argv
    for i, a in enumerate(argv):
        if a == "--target" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--target="):
            return a.split("=", 1)[1]
    return "device"


TARGET = os.environ["DECKBRIDGE_TARGET"] = _target()
# The firmware build embeds the React page when web/dist has one; tests of the old page's DOM skip then.
REACT_PAGE = (Path(__file__).resolve().parents[2] / "web/dist/index.html").exists()
SIM = Path(__file__).resolve().parents[2] / "firmware/sim/build/deckbridge_sim.elf"


WORKER = int(os.environ.get("PYTEST_XDIST_WORKER", "gw0")[2:])


def start_sim(port: int, **env: str) -> subprocess.Popen:
    """A simulator on 127.0.0.1:port that answers; DB_SIM_* settings come in `env`. Keep port below 22768: the
    simulator also takes port + 10000, and from 32768 up Linux hands out client ports."""
    with tempfile.TemporaryFile() as log:  # the simulator keeps its own handle
        sim = subprocess.Popen(
            [SIM], env={**os.environ, "DB_SIM_PORT": str(port), **env}, stdout=log, stderr=subprocess.STDOUT
        )
        atexit.register(sim.kill)
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
                return sim
            except OSError:
                if sim.poll() is not None:
                    break
                time.sleep(0.05)
        sim.kill()
        sim.wait()
        log.seek(0)
        raise RuntimeError(f"the simulator did not start:\n{log.read().decode(errors='replace')}")


def _start_sim() -> tuple[str, str]:
    port, password = 18080 + 2 * WORKER, "simulator-pass"
    start_sim(port, DB_SIM_PASSWORD=password)
    return f"127.0.0.1:{port}", password


if TARGET == "sim":
    HOST, PASSWORD = _start_sim()
else:
    HOST = need(DEVICE, "host")
    PASSWORD = password()
PORTS = (8765, 8766)


def pytest_addoption(parser):
    parser.addoption("--target", default="device", choices=("device", "sim"), help="the bridge, or firmware/sim")


_bench = contextlib.ExitStack()  # held until the process ends, after the atexit restore


def pytest_configure(config):
    if config.getoption("--target") != TARGET:
        pytest.exit(f"--target {config.getoption('--target')} but the tests were set up for {TARGET}", returncode=4)
    parallel = "PYTEST_XDIST_WORKER" in os.environ or getattr(config.option, "numprocesses", None)
    if TARGET == "device" and parallel:
        pytest.exit("one bridge cannot take tests in parallel: run the device suite without -n", returncode=4)
    if TARGET == "device":
        _bench.enter_context(bench_lock())  # deckmeter and other runs wait, and they wait for us


def pytest_collection_modifyitems(config, items):
    if TARGET == "sim":
        for item in items:
            if not item.get_closest_marker("sim"):
                item.add_marker(pytest.mark.skip(reason="needs the device"))


# the bridge's own certificate, which no CA signed
TLS = ssl.create_default_context()
TLS.check_hostname = False
TLS.verify_mode = ssl.CERT_NONE


def api(method: str, path: str, body: bytes | None = None, auth: bool = True, timeout: float = 30, https=False):
    real = json.loads(body).get("real") if path.startswith("/api/input") and body else None
    if real is not None:
        if not isinstance(real, str):
            raise UnsafeBridge('"real" other than a host address is for a person at the web UI: tests name the host')
        if ipaddress.ip_address(real) not in NOBODY and real != real_input_target(status()):
            raise UnsafeBridge(f'"real": {real} is not the host on this machine that draws')
    req = urllib.request.Request(
        f"{'https' if https else 'http'}://{HOST}{path}", data=body, method=method, headers={"X-DeckConnector": "1"}
    )
    if auth:
        req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=TLS if https else None) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


_sources = itertools.count()


def deck_page(
    method: str, path: str, body: bytes = b"", host: str = "", source: str = ""
) -> tuple[int, dict, http.client.HTTPMessage]:
    """An open request that puts a page on the deck. On the simulator each call comes from its own 127.0.x.1 unless
    `source` names one; a device waits out a held-off client."""
    url = urllib.parse.urlsplit(f"http://{host or HOST}")
    addr, port = url.hostname, url.port or 80
    for _ in range(2):
        src = source or (f"127.0.{next(_sources) % 250 + 1}.1" if TARGET == "sim" else "")
        c = http.client.HTTPConnection(addr, port, timeout=10, source_address=(src, 0) if src else None)
        c.request(method, path, body=body, headers={"X-DeckConnector": "1"})
        r = c.getresponse()
        reply = json.loads(r.read() or b"null")
        if r.status != 429 or source or (reply or {}).get("code") != "paused":
            return r.status, reply, r.headers
        time.sleep(int(r.headers["Retry-After"]))
    return r.status, reply, r.headers


def events(query: str = "") -> list[dict]:
    """GET /api/events, oldest first: the firmware pages newest first."""
    code, body = api("GET", "/api/events" + query)
    assert code == 200, body
    return list(reversed(json.loads(body)["events"]))


def status() -> dict:
    return json.loads(api("GET", "/api/status")[1])


signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))  # so `timeout` still runs the atexit restore


def put_config(body: dict, https: bool = False) -> None:
    """A refused change must not go unnoticed: a refused restore leaves the owner's deck on a test controller."""
    code, reply = api("PUT", "/api/config", json.dumps(body).encode(), https=https)
    if code != 200:
        raise RuntimeError(f"PUT /api/config {body} refused: {code} {reply[:200]!r}")


def get_config() -> dict:
    code, body = api("GET", "/api/config")
    if code == 403:  # https_only is on: plain HTTP refuses a password
        code, body = api("GET", "/api/config", https=True)
    if code != 200:
        raise RuntimeError(f"GET /api/config answered {code}")
    return json.loads(body)


# What tests change and a run sets back. Not the name or the network: changing those restarts the bridge.
KEPT = (
    "controller",
    "host",
    "satellite",
    "satellite_auto",
    "satellite_pin",
    "servers",
    "server_pins",
    "brightness",
    "menu_chord",
    "menu_hold_ms",
    "lang",
    "qr_login",
    "dock_paired_only",
    "https_only",
    "cors_origins",
    "usb_speed",
    "setup_done",
    "ntp_server",
    "screen_off_s",
    "trusted_proxy",
)


@pytest.fixture(scope="session", autouse=True)
def restore_config():
    """Everything a run changed goes back at its end, also on pytest.exit and SIGTERM (atexit)."""
    before = get_config()
    # A run killed before its teardown left the test controller behind once; never keep that as the one to restore.
    before |= settings_to_restore(before)
    done = []

    def restore():
        if done:
            return
        done.append(True)
        now = get_config()
        changed = {k: before[k] for k in KEPT if k in before and now.get(k) != before[k]}
        if changed:
            put_config(changed, https=now.get("https_only", False))

    atexit.register(restore)
    yield
    restore()


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
    need_camera()
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


def need_camera() -> None:
    if not CAMERA:
        pytest.skip("no camera: set camera in ~/.config/deckbridge/bench.json")


def camera() -> CameraStream:
    """One running stream for the whole run: a new connection costs about 6 s, waiting for a key frame."""
    global _stream
    need_camera()
    if _stream is None or _stream.proc.poll() is not None:
        _stream = CameraStream()
        atexit.register(_stream.close)
    return _stream


@pytest.fixture(scope="session", autouse=True)
def camera_warm():
    if CAMERA:
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
    cfg = get_config()
    if [u for u in cfg["servers"] if u] != wanted:
        put_config({"servers": wanted})  # applies without a restart
    put_config({"controller": "server", "host": ""})
    yield first, second
    await first.stop()
    await second.stop()
    put_config({k: cfg[k] for k in ("servers", "server_pins")} | settings_to_restore(cfg))


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
