"""Camera timings and traffic of a Stream Deck + behind the bridge or behind a real Elgato Network Dock, with the
same commands for both: the target is the bridge's or the dock's IP. What needs the bridge's API (simulated
presses, the Satellite and server paths, acks, boot_ms) runs only when the target answers it.

Needs the bench Companion on this machine (~/deckbridge-bench: companion.sh start, node
companion.mjs setup) and the camera that films the deck. Results go into --out as JSON, under --label.

uv run python -m dbtools.deckmeter camdelay --target 192.168.1.214 --out ESP.json      (bridge only, run first)
uv run python -m dbtools.deckmeter pages|image|input|restart|blip|boot|traffic --target IP --mode satellite|dock --out F

Every command that uses the deck holds /tmp/deckbridge-bench.lock and, on the bridge, sets the production settings
back at its end. `traffic --core HOST` only listens on that host.
"""

import argparse
import asyncio
import base64
import calendar
import contextlib
import fcntl
import io
import itertools
import json
import math
import os
import re
import signal
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

from dbtools.load import password
from dbtools.safety import PRODUCTION, real_input_target, require_input_gate
from dbtools.server import Server, local_ip

CAMERA = os.environ.get("DECKBRIDGE_CAMERA", "rtsp://192.168.1.163:8554/stream")
COMPANION = "http://127.0.0.1:8000"
CONTAINER = "companion-bench"
BENCH = Path(os.environ.get("DECKMETER_BENCH", Path.home() / "deckbridge-bench"))
CAPTURES = Path(os.environ.get("DECKMETER_CAPTURES", BENCH.parent / "captures"))
DRIVE = Path(__file__).resolve().parents[1] / "companion-check/drive.mjs"
LOCK = "/tmp/deckbridge-bench.lock"
PORTS = {"dock": {5343, 5344}, "satellite": {16622, 16623}}

# Centres of the keys and the four strip parts in the 1920x1080 camera picture of this bench.
REGIONS = {
    **{
        f"k{i}": xy
        for i, xy in enumerate(
            [(826, 320), (1074, 320), (1320, 320), (1566, 320), (830, 500), (1074, 500), (1320, 500), (1560, 500)]
        )
    },
    **{f"s{i}": (860 + 220 * i, 690) for i in range(4)},
}
STATIC = [f"k{i}" for i in range(7)]  # same on every redraw of a page; k7 shows $(custom:tick), the strip the presses
ALL = list(REGIONS)
SCALE = 4
HALF = 24
COLORS = [(176, 0, 0), (0, 160, 0), (0, 0, 192)]  # colour classes, as in the bench page set and drive.mjs


def stats(values: list) -> dict:
    v = sorted(x for x in values if x is not None)
    if not v:
        return {"n": 0, "missing": len(values)}
    return {
        "n": len(v),
        "median": round(v[len(v) // 2] if len(v) % 2 else (v[len(v) // 2 - 1] + v[len(v) // 2]) / 2, 1),
        "p90": round(v[math.ceil(0.9 * len(v)) - 1], 1),
        "min": round(v[0], 1),
        "max": round(v[-1], 1),
        "missing": len(values) - len(v),
    }


def ms(a, b):
    return None if a is None or b is None else round((a - b) * 1000, 1)


def dist(a, b) -> float:
    return math.dist(a, b)


def strip_is(f: dict, press: dict, want: int) -> bool:
    return nearest(f["s1"], press) == want and nearest(f["s2"], press) == want


def nearest(rgb, refs: dict):
    return min(refs, key=lambda k: dist(rgb, refs[k]))


class Camera:
    """The running stream, scaled down: every frame's arrival time and each region's mean colour."""

    W, H = 1920 // SCALE, 1080 // SCALE

    def __init__(self):
        h = HALF // SCALE
        self.boxes = [(x // SCALE - h, y // SCALE - h, x // SCALE + h, y // SCALE + h) for x, y in REGIONS.values()]
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
                f"scale={self.W}:{self.H}",
                "-f",
                "rawvideo",
                "-pix_fmt",
                "rgb24",
                "-",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.frames: list[tuple[float, dict]] = []
        self.last: bytes = b""
        threading.Thread(target=self._read, daemon=True).start()
        end = time.time() + 30
        while not self.frames:
            if time.time() > end:
                raise TimeoutError(f"no picture from {CAMERA}")
            time.sleep(0.1)

    def _read(self):
        size = self.W * self.H * 3
        while len(buf := self.proc.stdout.read(size)) == size:
            t = time.time()
            img = Image.frombytes("RGB", (self.W, self.H), buf)
            px = [img.crop(b).resize((1, 1), Image.BOX).getpixel((0, 0)) for b in self.boxes]
            self.frames.append((t, dict(zip(REGIONS, px, strict=True))))
            self.last = buf

    def mean(self, names, secs: float = 0.4) -> dict:
        since = time.time() - secs
        recent = [f for t, f in self.frames if t > since]
        if not recent:
            raise TimeoutError(f"the camera stream stalled: no frame for {time.time() - self.frames[-1][0]:.1f} s")
        return {n: tuple(sum(f[n][c] for f in recent) / len(recent) for c in range(3)) for n in names}

    def first(self, pred, after: float, timeout: float = 10, hold: int = 1) -> float | None:
        """Arrival time of the first frame after `after` from which `hold` frames in a row match."""
        end, i, run = time.time() + timeout, 0, 0
        while time.time() < end:
            while i < len(self.frames):
                t, f = self.frames[i]
                i += 1
                if t <= after:
                    continue
                run = run + 1 if pred(f) else 0
                if run == hold:
                    return self.frames[i - hold][0]
            time.sleep(0.01)
        return None

    def between(self, a: float, b: float) -> list:
        return [(t, f) for t, f in self.frames if a < t <= b]

    def snapshot(self, path: Path):
        img = Image.frombytes("RGB", (self.W, self.H), self.last).resize((1920, 1080))
        draw = ImageDraw.Draw(img)
        for name, (x, y) in REGIONS.items():
            draw.rectangle((x - HALF, y - HALF, x + HALF, y + HALF), outline=(255, 255, 255), width=3)
            draw.text((x - HALF, y - HALF - 14), name, fill=(255, 255, 255))
        img.save(path)

    def close(self):
        self.proc.kill()


class Http:
    """One keep-alive connection; Companion's HTTP API answers in about a millisecond on this machine."""

    def __init__(self, base: str, auth: str = ""):
        self.base, self.auth = base, auth

    def call(self, method: str, path: str, body=None, timeout: float = 10):
        data = json.dumps(body).encode() if isinstance(body, dict) else body
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if self.auth:
            req.add_header("Authorization", self.auth)
            req.add_header("X-DeckConnector", "1")
        if isinstance(body, dict):
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()


class Companion(Http):
    def __init__(self):
        super().__init__(COMPANION)

    def page(self, n: int):
        code, body = self.call("POST", f"/api/location/4/0/{n - 1}/press")
        if code != 200:
            raise RuntimeError(f"Companion page {n}: {code} {body[:100]!r}, is the bench page set imported?")

    def set_var(self, name: str, value):
        self.call("POST", f"/api/custom-variable/{name}/value?value={value}")

    def var(self, name: str) -> str:
        return self.call("GET", f"/api/custom-variable/{name}/value")[1].decode()

    def dock(self, ip: str):
        subprocess.run(["node", BENCH / "companion.mjs", "dock", ip], check=True, cwd=BENCH, capture_output=True)

    def log_after(self, since: float, pattern: str) -> float | None:
        out = subprocess.run(
            ["docker", "logs", "-t", "--since", f"{since - 1:.3f}", CONTAINER], capture_output=True, text=True
        ).stdout
        for line in out.splitlines():
            if re.search(pattern, line):
                stamp = line.split(" ", 1)[0]  # 2026-11-07T19:34:47.361436795Z, UTC
                t = calendar.timegm(time.strptime(stamp[:19], "%Y-%m-%dT%H:%M:%S")) + float(stamp[19:26])
                if t > since:
                    return t
        return None


class Bridge(Http):
    def __init__(self, ip: str):
        from dbtools.load import basic_auth

        super().__init__(f"http://{ip}", basic_auth(password()))

    def status(self) -> dict:
        return json.loads(self.call("GET", "/api/status")[1])

    def put(self, cfg: dict):
        code, body = self.call("PUT", "/api/config", cfg)
        if code != 200:
            raise RuntimeError(f"PUT /api/config {cfg} refused: {code} {body[:200]!r}")


def find_bridge(ip: str) -> Bridge | None:
    try:
        b = Bridge(ip)
        return b if "simulated_input" in b.status() else None
    except (OSError, ValueError):
        return None


class Run:
    """What every measurement needs: camera, Companion, the bridge when there is one, and the camera delay."""

    def __init__(self, a):
        self.a = a
        self.me = local_ip()
        self.bridge = None if a.no_bridge_api else find_bridge(a.target)
        self.comp = Companion()
        self.cam = Camera()
        self.delay = a.camera_delay_ms / 1000 if a.camera_delay_ms is not None else None
        self.refs: dict = {}

    def after(self, t: float) -> float:
        """Frames that arrive earlier show the deck from before t (one camera frame of slack)."""
        return t + self.delay - 0.02

    def visible(self, t):
        return None if t is None else t - self.delay

    def need_bridge(self, what: str):
        if not self.bridge:
            raise SystemExit(f"{what} needs the bridge's API at {self.a.target}")

    @contextlib.contextmanager
    def mode(self, mode: str):
        """Companion on this machine draws: over Satellite (bridge only) or as a Network Dock host."""
        try:
            if mode == "satellite":
                self.need_bridge("Satellite mode")
                self.comp.dock("off")
                self.bridge.put({"satellite": f"{self.me}:16622", "controller": "companion", "host": "satellite"})
            elif mode == "dock":
                self.comp.dock(self.a.target)
                if self.bridge:  # Satellite stays on production: the bridge refuses a dock host Satellite points to
                    self.bridge.put({"controller": "companion", "host": self.me})
            self.wait_mine()
            yield
        finally:
            if mode == "dock":
                self.comp.dock("off")
            self.restore()

    def wait_mine(self, timeout: float = 40):
        """On the bridge: until the host on this machine is the one that draws."""
        if not self.bridge:
            return
        end = time.time() + timeout
        while time.time() < end:
            s = self.bridge.status()
            drawing = [x["host"] for x in s["sources"] if x.get("drawing")]
            if len(drawing) == 1 and self.me in drawing[0]:
                return
            time.sleep(0.5)
        raise TimeoutError(f"this machine does not draw on the bridge: writer {s['writer']}, drawing {drawing}")

    def restore(self):
        if not self.bridge:
            return
        self.bridge.put(dict(PRODUCTION))
        end = time.time() + 30
        while time.time() < end:
            s = self.bridge.status()
            if s["writer"] == "satellite":
                print("bridge back on production, writer satellite")
                return
            time.sleep(1)
        print(f"WARNING: bridge on production settings, but writer is {s['writer']}")

    def learn(self, timeout: float = 60):
        """Show pages 3, 2, 1 and the colour classes, and keep what the camera sees as references. Also waits until
        Companion draws at all."""
        end, before = time.time() + timeout, None
        while time.time() < end:
            pages = {}
            for p in (3, 2, 1):
                self.comp.page(p)
                time.sleep(1.3)
                pages[p] = self.cam.mean(STATIC)
            pairs = ((1, 2), (2, 3), (1, 3))
            apart = all(sum(dist(pages[x][k], pages[y][k]) > 40 for k in STATIC) >= 6 for x, y in pairs)
            # two rounds that agree: the first may catch the deck before Companion draws, or mid-switch
            same = before and all(
                nearest(pages[p][k], {q: before[q][k] for q in before}) == p for p in pages for k in STATIC
            )
            if apart and same:
                break
            print(f"Companion's pages not seen yet (apart {apart}, same as before {bool(same)})")
            before = pages
        else:
            self.cam.snapshot(Path("/tmp/deckmeter-learn-failed.png"))
            if self.bridge:
                s = self.bridge.status()
                print("bridge:", s["writer"], [(x["host"], x["connected"], x["drawing"]) for x in s["sources"]])
            raise TimeoutError("Companion does not draw on the deck, see /tmp/deckmeter-learn-failed.png")
        tick, press = {}, {}
        for c in (0, 1, 2):
            self.comp.set_var("tick", c)
            time.sleep(1.0)
            tick[c] = self.cam.mean(["k7"])["k7"]
        for c in (0, 1):
            self.comp.set_var("presses", c)
            time.sleep(1.0)
            m = self.cam.mean(["s0", "s1", "s2", "s3"])
            press[c] = tuple(sum(m[s][i] for s in m) / 4 for i in range(3))
        self.refs = {"pages": pages, "tick": tick, "press": press}

    def on_page(self, f: dict) -> dict:
        refs = self.refs["pages"]
        got = [nearest(f[k], {p: refs[p][k] for p in refs}) for k in STATIC]
        return {p: got.count(p) for p in refs}

    def show(self, p: int, timeout: float = 15):
        self.comp.page(p)
        if not self.cam.first(lambda f: self.on_page(f)[p] == 7, time.time(), timeout, hold=3):
            raise TimeoutError(f"page {p} did not show")
        time.sleep(0.3)


# --- metrics -----------------------------------------------------------------------------------------------------


def m_pages(r: Run) -> dict:
    first, full = [], []
    with r.mode(r.a.mode):
        r.learn()
        r.show(1)
        for i in range(r.a.runs):
            p = 2 if i % 2 == 0 else 1
            time.sleep(1.0)
            t0 = time.time()
            r.comp.page(p)
            old = r.refs["pages"][3 - p]
            f1 = r.cam.first(lambda f, old=old: any(dist(f[k], old[k]) > 40 for k in STATIC), r.after(t0))
            f7 = r.cam.first(lambda f, p=p: r.on_page(f)[p] == 7, r.after(t0))
            first.append(ms(r.visible(f1), t0))
            full.append(ms(r.visible(f7), t0))
    return {
        "first_change_ms": stats(first),
        "all_keys_ms": stats(full),
        "samples_all_keys_ms": full,
        "how": "HTTP press on Companion's control page (internal set page) until the camera shows the new page",
    }


def analyse(r: Run, sends: list, regions: list, centroids: dict, acks: dict | None = None) -> dict:
    """Per rate: updates sent and seen per second, latency where a colour cannot be confused with the one three
    updates before, and how long after the last update of a burst the deck shows it."""
    if min(dist(centroids[a], centroids[b]) for a, b in ((0, 1), (1, 2), (0, 2))) < 40:
        raise RuntimeError(f"the camera did not see the three test colours ({centroids}): run it again")
    out = {}
    for rate in sorted({s["rate"] for s in sends}):
        group = [s for s in sends if s["rate"] == rate]
        t_first, t_last = group[0]["t"], group[-1]["t"]
        seen = r.cam.between(t_first + r.delay + 0.2, t_last + r.delay)
        classes = [nearest(f[regions[0]], centroids) for _, f in seen]
        changes = sum(1 for a, b in itertools.pairwise(classes) if a != b)
        span = seen[-1][0] - seen[0][0] if len(seen) > 1 else 0
        lat = []
        if 3 / rate >= 0.2:  # the same colour three updates earlier must be gone from the camera by then
            for s in group:
                t = r.cam.first(
                    lambda f, c=s["c"]: all(nearest(f[g], centroids) == c for g in regions), r.after(s["t"]), 3
                )
                lat.append(ms(r.visible(t), s["t"]))
        last = group[-1]
        t_end = r.cam.first(
            lambda f, c=last["c"]: all(nearest(f[g], centroids) == c for g in regions), r.after(last["t"]), 5, hold=3
        )
        entry = {
            "sent_per_s": round((len(group) - 1) / (t_last - t_first), 1) if len(group) > 1 else None,
            "seen_changes_per_s": round(changes / span, 1) if span else None,
            "last_update_ms": ms(r.visible(t_end), last["t"]),
        }
        if lat:
            entry["latency_ms"] = stats(lat)
        if "done" in group[0]:
            entry["send_call_ms"] = stats([(s["done"] - s["t"]) * 1000 for s in group])
        if acks:
            entry["ack_ms"] = stats([acks[id(s)] for s in group if id(s) in acks])
        out[str(rate)] = entry
    out["centroids"] = {c: [round(x) for x in v] for c, v in centroids.items()}
    return out


def burst(rates, secs, draw) -> list:
    sends = []
    for rate in rates:
        start = time.time()
        for i in range(round(rate * secs)):
            due = start + i / rate
            if due > time.time():
                time.sleep(due - time.time())
            s = {"rate": rate, "i": i, "c": i % 3, "t": time.time()}
            draw(s)
            s["done"] = time.time()
            sends.append(s)
        time.sleep(1.2)
    return sends


def learn_classes(r: Run, draw, regions) -> dict:
    cents = {}
    for c in (0, 1, 2):
        draw({"c": c})
        time.sleep(1.2)
        m = r.cam.mean(regions)
        cents[c] = tuple(sum(m[g][i] for g in regions) / len(regions) for i in range(3))
    return cents


def rgb_bitmap(c: int, w: int, h: int) -> str:
    return base64.b64encode(bytes(COLORS[c]) * w * h).decode()


class FakeSatellite(socketserver.ThreadingTCPServer):
    """Companion's Satellite API, enough for the bridge to connect and take pictures (as test_satellite_fake)."""

    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, port: int):
        srv = self
        srv.device, srv.conn = None, None

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                srv.conn = self.connection
                self.wfile.write(b"BEGIN CompanionVersion=fake ApiVersion=1.16.0\n")
                self.wfile.write(b"CAPS BITMAP_FORMATS=rgb ROTARY_AMOUNT=1\n")
                for raw in self.rfile:
                    line = raw.decode().strip()
                    if line.startswith("ADD-DEVICE"):
                        srv.device = line.split('DEVICEID="', 1)[1].split('"', 1)[0]
                        self.wfile.write(f'ADD-DEVICE OK DEVICEID="{srv.device}"\n'.encode())
                    elif line.startswith("PING"):
                        self.wfile.write(b"PONG\n")

        super().__init__(("0.0.0.0", port), Handler)
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def key(self, control: str, c: int, w: int, h: int) -> str:
        return f'KEY-STATE DEVICEID="{self.device}" CONTROLID="{control}" BITMAP="{rgb_bitmap(c, w, h)}"\n'


class Loop:
    """An asyncio loop in a thread, for dbtools.server from plain code."""

    def __init__(self):
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()

    def run(self, coro, timeout: float = 60):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout)


def jpeg(c: int, size=(800, 480)) -> bytes:
    buf = io.BytesIO()
    Image.new("RGB", size, COLORS[c]).save(buf, "JPEG", quality=80)
    return buf.getvalue()


@contextlib.contextmanager
def server_source(r: Run):
    """The bridge draws for a dbtools.server on this machine (controller server), with acks after USB."""
    r.need_bridge("the server path")
    loop = Loop()
    srv = loop.run(Server(8765, "deckmeter", ping=True).start())
    cfg = json.loads(r.bridge.call("GET", "/api/config")[1])
    try:
        if srv.url not in cfg["servers"]:
            r.bridge.put({"servers": [srv.url, "", "", ""]})
        r.bridge.put({"controller": "server", "host": ""})
        bridge = loop.run(srv.wait_bridge(30))
        end = time.time() + 15
        while not bridge.active and time.time() < end:
            time.sleep(0.1)
        yield loop, bridge
    finally:
        if srv.url not in cfg["servers"]:
            r.bridge.put({"servers": cfg["servers"]})
        r.restore()
        loop.run(srv.stop())


def m_camdelay(r: Run) -> dict:
    """The camera's own delay: a picture on key 8 whose ack says it left over USB, until the camera shows it. A key,
    not the full screen: the deck shows a small picture sooner, as Companion's updates are."""
    rect = (670, 172, 120, 120)
    pics = {c: jpeg(c, (120, 120)) for c in (0, 1, 2)}
    with server_source(r) as (loop, bridge):
        loop.run(bridge.send_image((0, 0, 800, 480), jpeg(0)))
        time.sleep(1)
        cents = learn_classes(r, lambda s: loop.run(bridge.send_image(rect, pics[s["c"]])), ["k7"])
        delays = []
        for i in range(r.a.runs):
            c = (i + 1) % 3
            loop.run(bridge.send_image(rect, pics[c]))
            acked = time.time()
            seen = r.cam.first(lambda f, c=c: nearest(f["k7"], cents) == c, acked, 5)
            delays.append(ms(seen, acked))
            time.sleep(0.8)
    return {
        "delay_ms": stats(delays),
        "samples_ms": delays,
        "centroids": {c: [round(x) for x in v] for c, v in cents.items()},
        "how": "server protocol JPEG on key 8, ack after the USB transfer, until the camera shows it",
    }


def m_image(r: Run) -> dict:
    a = r.a
    rates = [float(x) for x in a.rates.split(",")]
    regions = ["k7"] if a.kind == "key" else ALL
    if a.source == "companion":
        if a.kind != "key":
            raise SystemExit("Companion redraws the whole deck only on a page switch: see the pages command")
        with r.mode(a.mode):
            r.learn()
            r.show(1)
            sends = burst(rates, a.secs, lambda s: r.comp.set_var("tick", s["i"]))  # tick % 3 is the class
            cents = r.refs["tick"]
            return {"source": "companion", "kind": a.kind, "rates": analyse(r, sends, regions, cents)}
    if a.source == "dock":
        r.comp.dock("off")
        try:
            if r.bridge:
                r.bridge.put({"controller": "companion", "host": r.me})
            out = subprocess.run(
                ["node", DRIVE, a.target, a.kind, a.rates, str(a.secs)], capture_output=True, text=True, timeout=600
            )
            lines = [json.loads(x) for x in out.stdout.splitlines() if x.startswith("{")]
            if not lines:
                raise RuntimeError(f"drive.mjs: {out.stderr[-500:]}")
            learned = {x["learn"]: x["t"] for x in lines if "learn" in x}
            cents = {}
            for c, t in learned.items():
                fr = r.cam.between(t + r.delay + 0.5, t + r.delay + 1.2)
                n = len(fr) * len(regions)
                cents[c] = tuple(sum(f[g][i] for _, f in fr for g in regions) / n for i in range(3))
            sends = [x for x in lines if "rate" in x]
            time.sleep(r.delay + 1)
            return {
                "source": "dock client (@elgato-stream-deck/tcp)",
                "kind": a.kind,
                "rates": analyse(r, sends, regions, cents),
            }
        finally:
            r.restore()
    if a.source == "satellite":
        r.need_bridge("the Satellite fake")
        fake = FakeSatellite(16701)
        try:
            r.bridge.put({"satellite": f"{r.me}:16701", "controller": "companion", "host": "satellite"})
            end = time.time() + 30
            while not fake.device and time.time() < end:
                time.sleep(0.2)
            r.wait_mine()
            controls = (
                [("1/3", 120, 120)]
                if a.kind == "key"
                else ([(f"{i // 4}/{i % 4}", 120, 120) for i in range(8)] + [(f"2/{i}", 200, 100) for i in range(4)])
            )
            pics = {c: "".join(fake.key(k, c, w, h) for k, w, h in controls).encode() for c in (0, 1, 2)}
            draw = lambda s: fake.conn.sendall(pics[s["c"]])  # noqa: E731
            cents = learn_classes(r, draw, regions)
            sends = burst(rates, a.secs, draw)
            return {"source": "satellite fake", "kind": a.kind, "rates": analyse(r, sends, regions, cents)}
        finally:
            r.restore()
            fake.shutdown()
            fake.server_close()
    if a.source == "server":
        with server_source(r) as (loop, bridge):
            if a.kind == "key":
                rect, pics = (670, 172, 120, 120), {c: jpeg(c, (120, 120)) for c in (0, 1, 2)}
            else:
                rect, pics = (0, 0, 800, 480), {c: jpeg(c) for c in (0, 1, 2)}
            cents = learn_classes(r, lambda s: loop.run(bridge.send_image(rect, pics[s["c"]])), regions)
            acks, futs = {}, []

            def draw(s):
                fut = loop.run(bridge.post_image(rect, pics[s["c"]]))
                fut.add_done_callback(lambda _f, s=s: acks.__setitem__(id(s), (time.time() - s["t"]) * 1000))
                futs.append(fut)

            sends = burst(rates, a.secs, draw)
            res = analyse(r, sends, regions, cents, acks)
            return {"source": "server protocol (acks after USB)", "kind": a.kind, "rates": res}
    raise SystemExit(f"unknown source {a.source}")


def m_input(r: Run) -> dict:
    with r.mode(r.a.mode):
        r.learn()
        r.show(1)
        if r.a.manual:
            return manual_presses(r)
        r.need_bridge("simulated presses")
        status = r.bridge.status()
        require_input_gate(status)
        me = real_input_target(status)  # refuses unless the one host that draws is on this machine
        reg, fb, call = [], [], []
        press = r.refs["press"]
        for _ in range(r.a.runs):
            time.sleep(1.0)
            before = int(r.comp.var("presses") or 0)
            want = (before + 1) % 2
            t0 = time.time()
            r.bridge.call("POST", "/api/input", {"key": 0, "action": "down", "real": me})
            call.append(ms(time.time(), t0))
            t_reg = None
            while time.time() - t0 < 3:
                if int(r.comp.var("presses") or 0) != before:
                    t_reg = time.time()
                    break
            r.bridge.call("POST", "/api/input", {"key": 0, "action": "up", "real": me})
            seen = r.cam.first(lambda f, w=want: strip_is(f, press, w), r.after(t0), 3, 2)
            reg.append(ms(t_reg, t0))
            fb.append(ms(r.visible(seen), t0))
        return {
            "to_companion_ms": stats(reg),
            "to_feedback_ms": stats(fb),
            "api_call_ms": stats(call),
            "how": "POST /api/input key 1 down bound to this machine; Companion custom variable polled over HTTP; "
            "strip colour on the camera",
        }


def manual_presses(r: Run) -> dict:
    """A person presses key 1 on page 1 a few times, a second or more apart. Key 1 counts presses in Companion and
    the strip flips colour. The camera times the finger reaching the key against the strip flip."""
    print(f"press key 1 (top left) every 2 s or so, for {r.a.secs:.0f} s")
    changes, start = [], time.time()
    last = r.comp.var("presses")
    while time.time() - start < r.a.secs:
        now = r.comp.var("presses")
        if now != last:
            changes.append((time.time(), int(now)))
            last = now
        time.sleep(0.003)
    time.sleep(r.delay + 1)
    press, key0 = r.refs["press"], r.refs["pages"][1]["k0"]
    touch_fb, touch_reg = [], []
    for t_reg, n in changes:
        want = n % 2
        fb = r.cam.first(lambda f, w=want: strip_is(f, press, w), t_reg + r.delay - 0.4, 2, 2)
        # the finger: the first frame before the flip in which key 1 no longer looks like itself
        window = r.cam.between(t_reg + r.delay - 1.0, fb or t_reg + r.delay)
        touch = next((t for t, f in window if dist(f["k0"], key0) > 40), None)
        touch_fb.append(ms(fb, touch))
        touch_reg.append(ms(t_reg, r.visible(touch)))
    return {
        "presses": len(changes),
        "touch_to_feedback_ms": stats(touch_fb),
        "touch_to_companion_ms": stats(touch_reg),
        "how": "camera: finger covers key 1 -> strip flips (camera to camera, no delay subtracted); "
        "Companion custom variable polled over HTTP against the touch minus the camera delay",
    }


def m_restart(r: Run) -> dict:
    lost, back = [], []
    with r.mode(r.a.mode):
        r.learn()
        for _ in range(r.a.runs):
            r.show(2)  # Companion comes back on its startup page 1, so the camera tells its first picture apart
            t_stop = time.time()
            subprocess.run(["docker", "stop", "-t", "5", CONTAINER], check=True, capture_output=True)
            t_lost = r.cam.first(lambda f: r.on_page(f)[2] <= 2, t_stop, 5)
            lost.append(ms(r.visible(t_lost), t_stop))
            time.sleep(2)
            t0 = time.time()
            subprocess.run(["docker", "start", CONTAINER], check=True, capture_output=True)
            seen = r.cam.first(lambda f: r.on_page(f)[1] == 7, t0, 90, hold=3)
            back.append(ms(r.visible(seen), t0))
            time.sleep(2)
    return {
        "stop_to_picture_gone_ms": stats(lost),
        "start_to_first_page_ms": stats(back),
        "samples_ms": back,
        "how": "docker stop, 2 s, docker start of the bench Companion until the camera shows its startup page",
    }


def iptables(op: str, ips: list):
    for ip in ips:
        subprocess.run(["sudo", "-n", "iptables", op, "INPUT", "-s", ip, "-j", "DROP"], check=op == "-I")
        subprocess.run(["sudo", "-n", "iptables", op, "OUTPUT", "-d", ip, "-j", "DROP"], check=op == "-I")


def m_blip(r: Run) -> dict:
    out = {}
    with r.mode(r.a.mode):
        ips = [r.a.target]
        if r.bridge and (wlan := r.bridge.status()["network"]["wlan"].get("ip")):
            ips.append(wlan)  # or the bridge would carry on over WLAN
        r.learn()
        for secs in [float(x) for x in r.a.blips.split(",")]:
            back, pages, lost = [], [], []
            for _ in range(r.a.runs):
                r.show(1)
                t_start = time.time()
                try:
                    iptables("-I", ips)
                    time.sleep(secs / 2)
                    r.comp.page(2)
                    time.sleep(secs / 2)
                finally:
                    iptables("-D", ips)
                t0 = time.time()
                # a dropped session comes back on Companion's startup page 1, a kept one shows page 2
                gone = r.cam.first(lambda f: max(r.on_page(f).values()) < 6, r.after(t_start), t0 - t_start)
                drawn = lambda f, gone=gone: r.on_page(f)[2] == 7 or (gone and r.on_page(f)[1] == 7)  # noqa: E731
                seen = r.cam.first(drawn, r.after(max(t0, gone or 0)), 90, hold=3)
                back.append(ms(r.visible(seen), t0))
                pages.append(
                    None
                    if seen is None
                    else max(r.on_page(r.cam.between(seen - 0.01, seen)[0][1]).items(), key=lambda x: x[1])[0]
                )
                lost.append(ms(r.visible(gone), t_start))
            out[f"{secs:g}s"] = {
                "end_to_drawing_ms": stats(back),
                "samples_ms": back,
                "page_after": pages,
                "deck_left_companion_after_ms": lost,
            }
    out["how"] = (
        "this machine drops all packets to and from the target (LAN and WLAN address) for the time; Companion "
        "switches to page 2 halfway; timed from the end of the blip until the deck shows Companion again (page 2, "
        "or page 1 when the session was dropped and Companion started the deck afresh)"
    )
    return out


def m_boot(r: Run) -> dict:
    from dbtools.poe import power_cut

    runs = []
    with r.mode(r.a.mode):
        r.learn()
        pattern = r"Adding Satellite device" if r.a.mode == "satellite" else r.a.dock_log
        for i in range(r.a.runs):
            if i:
                time.sleep(60)  # real cuts a minute apart, for the hardware
            r.show(2)
            lit = lum(r.cam.frames[-1][1])
            t0 = time.time()
            power_cut(usb_unplugged=r.a.usb_unplugged, port=r.a.port)
            # dark: the keys lose power (a black key matches some page's colour, so brightness, not pages)
            dark = r.cam.first(lambda f, lit=lit: lum(f) < lit / 4, t0, 30, hold=3)
            floor = min((lum(f) for _, f in r.cam.between(dark, dark + 0.5)), default=0) if dark else 0
            # light: the deck's backlight comes back, when the bridge or dock opens the deck
            on = r.cam.first(lambda f, floor=floor: lum(f) > floor + 15, dark or t0, 60, hold=3)
            back = r.cam.first(lambda f: 7 in (r.on_page(f)[1], r.on_page(f)[2]), on or t0, 120, hold=5)
            conn = r.comp.log_after(t0, pattern)
            run = {
                "cmd_to_dark_ms": ms(dark, t0 + r.delay),
                "dark_to_light_ms": ms(on, dark),
                "dark_to_host_picture_ms": ms(back, dark),
                "light_to_host_picture_ms": ms(back, on),
                "light_to_companion_connected_ms": ms(conn, r.visible(on)),
            }
            if r.bridge:
                with contextlib.suppress(Exception):
                    run["bridge_boot_ms"] = r.bridge.status()["boot_ms"]
            print(run)
            runs.append(run)
    keys = [k for k in runs[0] if k.endswith("_ms") and k != "bridge_boot_ms"]
    return {
        **{k: stats([x[k] for x in runs]) for k in keys},
        "runs": runs,
        "how": f"PoE power-cycle of switch port {r.a.port}; dark = the keys lose power; light = the deck's "
        "backlight is back (the bridge or dock opened the deck); host picture = Companion's page on all keys; "
        "connected = Companion log line. Dark to picture includes the switch's off time, the same for both devices",
    }


def lum(f: dict) -> float:
    return sum(sum(f[k]) for k in ALL) / (3 * len(ALL))


def pcap_packets(path: Path):
    """(time, src, dst, frame length, payload length, ports) of each IPv4 packet in a pcap (Ethernet or Linux
    cooked); ports is (source, destination) for TCP and UDP, else ()."""
    data = path.read_bytes()
    magic = data[:4]
    end = "<" if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1") else ">"
    nano = magic in (b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\x3c\x4d")
    link = struct.unpack(end + "I", data[20:24])[0]
    off = 24
    while off + 16 <= len(data):
        sec, frac, incl, orig = struct.unpack(end + "IIII", data[off : off + 16])
        pkt = data[off + 16 : off + 16 + incl]
        off += 16 + incl
        if link == 1:
            eth, l3 = struct.unpack("!H", pkt[12:14])[0], 14
            if eth == 0x8100:
                eth, l3 = struct.unpack("!H", pkt[16:18])[0], 18
        elif link == 113:
            eth, l3 = struct.unpack("!H", pkt[14:16])[0], 16
        else:
            raise ValueError(f"link type {link}")
        if eth != 0x0800 or len(pkt) < l3 + 20:
            continue
        ihl = (pkt[l3] & 15) * 4
        total, proto = struct.unpack("!H", pkt[l3 + 2 : l3 + 4])[0], pkt[l3 + 9]
        src, dst = socket.inet_ntoa(pkt[l3 + 12 : l3 + 16]), socket.inet_ntoa(pkt[l3 + 16 : l3 + 20])
        ports = struct.unpack("!HH", pkt[l3 + ihl : l3 + ihl + 4]) if proto in (6, 17) else ()
        if proto == 6 and len(pkt) >= l3 + ihl + 13:
            payload = total - ihl - (pkt[l3 + ihl + 12] >> 4) * 4
        elif proto == 17:
            payload = total - ihl - 8
        else:
            payload = total - ihl
        yield sec + frac / (1e9 if nano else 1e6), src, dst, orig, payload, ports


def traffic_summary(path: Path, peer: set, phases: list, ports: set) -> dict:
    """Per phase and direction, on the host connection's ports only: bytes and packets per second, payload sizes,
    and the gaps between payload packets (in an idle phase, the keepalive interval)."""
    pkts = [p for p in pcap_packets(path) if ports & set(p[5])]
    out = {}
    for name, a, b in phases:
        res = {"secs": round(b - a, 1), "start": a, "end": b}
        for way, mine in (("from_target", lambda p: p[1] in peer), ("to_target", lambda p: p[2] in peer)):
            sel = [p for p in pkts if a <= p[0] < b and mine(p)]
            data = [p for p in sel if p[4] > 0]
            gaps = [(y[0] - x[0]) * 1000 for x, y in itertools.pairwise(data)]
            res[way] = {
                "bytes_per_s": round(sum(p[3] for p in sel) / (b - a)),
                "packets_per_s": round(len(sel) / (b - a), 1),
                "payload_packets": len(data),
                "payload_bytes": stats([p[4] for p in data]),
                "gap_between_payload_packets_ms": stats(gaps),
            }
        out[name] = res
    return out


def m_traffic(r: Run) -> dict:
    a = r.a
    CAPTURES.mkdir(parents=True, exist_ok=True)
    name = f"{a.label}-{time.strftime('%Y%m%d-%H%M%S')}.pcap"
    with r.mode(a.mode):
        peer = {a.target}
        if r.bridge and (wlan := r.bridge.status()["network"]["wlan"].get("ip")):
            peer.add(wlan)
        r.learn()
        r.show(1)
        flt = " or ".join(f"host {ip}" for ip in sorted(peer))
        subprocess.run(["docker", "rm", "-f", "deckmeter-tcpdump"], capture_output=True)
        subprocess.run(
            [
                "docker",
                "run",
                "-d",
                "--name",
                "deckmeter-tcpdump",
                "--net",
                "host",
                "-v",
                f"{CAPTURES}:/cap",
                "nicolaka/netshoot",
                "tcpdump",
                "-i",
                a.iface,
                "-w",
                f"/cap/{name}",
                flt,
            ],
            check=True,
            capture_output=True,
        )
        try:
            time.sleep(3)
            phases = []
            t = time.time()
            time.sleep(a.secs)
            phases.append(("idle", t, time.time()))
            t = time.time()
            for i in range(200):  # 10 changes per second of one key, for 20 s
                r.comp.set_var("tick", i)
                time.sleep(0.1)
            phases.append(("key_10_per_s", t, time.time()))
            t = time.time()
            for i in range(10):  # a page switch per second
                r.comp.page(2 if i % 2 == 0 else 1)
                time.sleep(1)
            phases.append(("page_switch_1_per_s", t, time.time()))
            time.sleep(1)
        finally:
            subprocess.run(["docker", "stop", "deckmeter-tcpdump"], capture_output=True)
            subprocess.run(["docker", "rm", "-f", "deckmeter-tcpdump"], capture_output=True)
    pcap = CAPTURES / name
    return {
        "pcap": str(pcap),
        "filter": flt,
        "ports": sorted(PORTS[a.mode]),
        "phases": traffic_summary(pcap, peer, phases, PORTS[a.mode]),
    }


def core_traffic(a) -> dict:
    """The production Satellite connection, captured on core for a while: only listening, nothing pressed."""
    CAPTURES.mkdir(parents=True, exist_ok=True)
    pcap = CAPTURES / f"core-satellite-{time.strftime('%Y%m%d-%H%M%S')}.pcap"
    t = time.time()
    with pcap.open("wb") as f:
        subprocess.run(
            ["ssh", a.core, f"timeout {a.secs:.0f} tcpdump -i eth0 -U -w - host {a.target} 2>/dev/null"],
            stdout=f,
            check=False,
        )
    return {
        "pcap": str(pcap),
        "phases": traffic_summary(pcap, {a.target}, [("production", t, time.time())], {443}),
        "how": f"tcpdump on {a.core} eth0, host {a.target}, while the owner's Companion drew as usual",
    }


def save(path: Path, label: str, metric: str, result: dict):
    data = json.loads(path.read_text()) if path.exists() else {}
    data.setdefault(label, {})[metric] = {"at": time.strftime("%Y-%m-%dT%H:%M:%S%z"), **result}
    path.write_text(json.dumps(data, indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "metric", choices=["camdelay", "pages", "image", "input", "restart", "blip", "boot", "traffic", "snapshot"]
    )
    ap.add_argument("--target", default="192.168.1.214", help="the bridge or the Network Dock")
    ap.add_argument("--mode", choices=["satellite", "dock"], default="dock")
    ap.add_argument("--out", type=Path, help="JSON file the results are added to")
    ap.add_argument("--label", help="key in --out, default esp-<mode> or dock-<mode>")
    ap.add_argument("--camera-delay-ms", type=float, help="default: camera.camdelay in --out")
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--no-bridge-api", action="store_true", help="treat the bridge like an Elgato dock")
    ap.add_argument("--source", choices=["companion", "dock", "satellite", "server"], default="dock")
    ap.add_argument("--kind", choices=["key", "page"], default="key")
    ap.add_argument("--rates", default="2,5,10,20,30,60")
    ap.add_argument("--secs", type=float, default=4, help="per rate (image), idle phase (traffic), listening (manual)")
    ap.add_argument("--manual", action="store_true", help="input: a person presses, nothing is simulated")
    ap.add_argument("--blips", default="3,15", help="seconds without network, comma separated")
    ap.add_argument("--port", type=int, default=4, help="boot: switch port, 4 the bridge, 2 the Network Dock")
    ap.add_argument("--usb-unplugged", action="store_true", help="boot on port 4: the bridge's USB cable is out")
    ap.add_argument(
        "--dock-log",
        default=r"StreamDeck connected: Stream Deck [^N]",
        help="boot in dock mode: Companion log line that means the deck is there",
    )
    ap.add_argument("--iface", default="eth0")
    ap.add_argument("--core", help="traffic: capture on this ssh host instead (production, nothing pressed)")
    a = ap.parse_args()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))  # so `timeout` still sets the bridge back
    if a.camera_delay_ms is None and a.out and a.out.exists() and a.metric != "camdelay":
        with contextlib.suppress(KeyError):
            a.camera_delay_ms = json.loads(a.out.read_text())["camera"]["camdelay"]["delay_ms"]["median"]
    if a.core:
        result = core_traffic(a)
        print(json.dumps(result, indent=1))
        if a.out:
            save(a.out, a.label or "core", "traffic", result)
        return
    with open(LOCK, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        r = Run(a)
        direct = a.metric == "image" and a.source != "companion"
        device = "esp" if r.bridge else "dock"
        a.label = a.label or ("camera" if a.metric == "camdelay" else f"{device}-{'direct' if direct else a.mode}")
        if a.metric == "snapshot":
            time.sleep(1)
            r.cam.snapshot(Path("deckmeter-regions.png"))
            print("deckmeter-regions.png", r.cam.mean(ALL))
            return
        if a.metric != "camdelay" and r.delay is None:
            raise SystemExit("needs --camera-delay-ms, or a camdelay result in --out")
        try:
            result = globals()[f"m_{a.metric}"](r)
        finally:
            r.cam.close()
        result["camera_delay_ms"] = None if r.delay is None else round(r.delay * 1000)
        print(json.dumps(result, indent=1))
        if a.out:
            save(a.out, a.label, a.metric if a.metric != "image" else f"image-{a.source}-{a.kind}", result)


if __name__ == "__main__":
    main()
