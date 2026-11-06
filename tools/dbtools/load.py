"""Load test for the HTTP API: latency per endpoint, throughput, and several Home Assistant instances at once.

uv run python -m dbtools.load [--host 192.168.1.214] [--secs 20]
uv run python -m dbtools.load --json B0.json      adds this run's numbers to a baseline file (several runs = noise band)
uv run python -m dbtools.load --compare B0.json new.json   lists what got worse beyond the noise; exit 1 if anything did

--json also measures what needs a server to draw (sets the controller to server and back) and the firmware size of
firmware/build.
"""

import argparse
import asyncio
import base64
import http.client
import json
import os
import statistics
import subprocess
import threading
import time
from collections import Counter
from pathlib import Path

from websockets.sync.client import connect

ENDPOINTS = ["/api/status", "/api/config", "/api/screen", "/api/screen.jpg", "/api/metrics", "/api/slots", "/"]
failures: Counter = Counter()  # every failed attempt by type, also the ones a retry hid
failures_lock = threading.Lock()


def failed(what: str) -> None:
    with failures_lock:
        failures[what] += 1


def report_failures() -> str:
    with failures_lock:
        text = ", ".join(f"{k} {v}" for k, v in failures.most_common()) or "none"
        failures.clear()
    return f"failed attempts: {text}"


class Client:
    """One keep-alive connection, like the session of one Home Assistant instance."""

    def __init__(self, host: str, auth: str):
        self.host, self.auth = host, auth
        self.conn = None

    def get(self, path: str) -> tuple[int, float, bytes]:
        t = time.perf_counter()
        for attempt in range(2):  # a connection the bridge closed in between gets one fresh try
            try:
                self.conn = self.conn or http.client.HTTPConnection(self.host, timeout=10)
                self.conn.request("GET", path, headers={"Authorization": self.auth})
                r = self.conn.getresponse()
                body = r.read()
                return r.status, (time.perf_counter() - t) * 1000, body
            except (OSError, http.client.HTTPException) as e:
                failed(type(e).__name__)
                self.conn.close()
                self.conn = None
                if attempt:
                    return 0, (time.perf_counter() - t) * 1000, b""
        raise AssertionError


def quantiles(ms: list[float]) -> dict:
    q = statistics.quantiles(ms, n=100) if len(ms) > 1 else [ms[0]] * 99
    return {"p50": round(q[49], 2), "p95": round(q[94], 2)}


def summary(ms: list[float], errors: int) -> str:
    if not ms:
        return f"no answers, {errors} errors"
    q = statistics.quantiles(ms, n=100) if len(ms) > 1 else [ms[0]] * 99
    return f"n={len(ms):4} p50={q[49]:6.1f} p95={q[94]:6.1f} max={max(ms):6.1f} ms  errors={errors}"


def latency(host: str, auth: str, n: int) -> dict:
    print("\n# One client, one request after the other")
    c = Client(host, auth)
    images = json.loads(c.get("/api/screen")[2])["images"]
    out = {}
    for path in ENDPOINTS + ([images[0]["url"]] if images else []):
        ms, errors = [], 0
        for _ in range(n // 4 if path.endswith(".jpg") else n):
            code, t, _ = c.get(path)
            if code == 200:
                ms.append(t)
            else:
                errors += 1
        print(f"{path:16} {summary(ms, errors)}  {report_failures()}")
        if ms:
            out["/api/screen/<id>.jpg" if path.startswith("/api/screen/") else path] = quantiles(ms)
    return out


def hammer(host: str, auth: str, path: str, threads: int, secs: float) -> dict:
    ms, errors, lock = [], [0], threading.Lock()
    end = time.time() + secs

    def run():
        c = Client(host, auth)
        while time.time() < end:
            code, t, _ = c.get(path)
            with lock:
                if code == 200:
                    ms.append(t)
                else:
                    errors[0] += 1

    workers = [threading.Thread(target=run) for _ in range(threads)]
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    print(f"{path:16} x{threads}: {len(ms) / secs:6.1f} req/s  {summary(ms, errors[0])}  {report_failures()}")
    return {"rps": round(len(ms) / secs, 1), "errors": errors[0], **(quantiles(ms) if ms else {})}


def home_assistants(host: str, auth: str, count: int, secs: float) -> dict:
    """Each instance holds a web socket, polls the status every second and the camera every 2 seconds."""
    stats = {"status": [], "camera": [], "errors": 0, "ws_msgs": 0, "ws_drops": 0}
    lock = threading.Lock()
    end = time.time() + secs

    def instance():
        c = Client(host, auth)
        next_cam = 0.0
        while time.time() < end:
            code, t, _ = c.get("/api/status")
            with lock:
                stats["status" if code == 200 else "errors"] += [t] if code == 200 else 1
            if time.time() >= next_cam:
                next_cam = time.time() + 2
                code, t, _ = c.get("/api/screen.jpg")
                with lock:
                    stats["camera" if code == 200 else "errors"] += [t] if code == 200 else 1
            time.sleep(1)

    def listener():
        try:
            with connect(f"ws://{host}/api/ws", additional_headers={"Authorization": auth}, open_timeout=10) as ws:
                while time.time() < end:
                    try:
                        ws.recv(timeout=1)
                        with lock:
                            stats["ws_msgs"] += 1
                    except TimeoutError:
                        pass
        except Exception as e:  # the type says whether the bridge refused, dropped or the client broke
            failed("ws " + type(e).__name__)
            with lock:
                stats["ws_drops"] += 1

    workers = [threading.Thread(target=f) for _ in range(count) for f in (instance, listener)]
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    print(
        f"{count} instances: status {summary(stats['status'], 0)}\n"
        f"{'':12} camera {summary(stats['camera'], 0)}\n"
        f"{'':12} errors={stats['errors']} ws_messages={stats['ws_msgs']} ws_refused_or_dropped={stats['ws_drops']}\n"
        f"{'':12} {report_failures()}"
    )
    return {
        "status_p95": quantiles(stats["status"])["p95"] if stats["status"] else None,
        "errors": stats["errors"],
        "ws_drops": stats["ws_drops"],
    }


def ws_connect(host: str, auth: str, n: int) -> dict:
    """From opening the socket to the first live message: what a page or Home Assistant waits for."""
    ms = []
    for _ in range(n):
        t = time.perf_counter()
        with connect(f"ws://{host}/api/ws", additional_headers={"Authorization": auth}, open_timeout=10) as ws:
            ws.recv(timeout=5)
            ms.append((time.perf_counter() - t) * 1000)
    print(f"ws connect to first message {summary(ms, 0)}")
    return quantiles(ms)


async def drawing(host: str, auth: str, n: int) -> dict:
    """Image and input round trips through a server that draws. Sets the controller to server and back."""
    from PIL import Image

    from .bench import jpeg
    from .server import Server, now_us

    c = Client(host, auth)

    def send(method: str, path: str, body: dict) -> None:
        c.get("/api/status")
        c.conn.request(method, path, json.dumps(body), {"Authorization": auth, "X-Deckbridge": "1"})
        r = c.conn.getresponse()
        reply = r.read()
        if r.status != 200:
            raise RuntimeError(f"{path} refused: {r.status} {reply[:200]!r}")

    from .safety import require_input_gate

    require_input_gate(json.loads(c.get("/api/status")[2]))
    cfg = json.loads(c.get("/api/config")[2])
    server = await Server(8765, "load", ping=False).start()
    await asyncio.to_thread(send, "PUT", "/api/config", {"controller": "server", "host": server.url})
    try:
        bridge = await server.wait_bridge(60)
        for _ in range(100):
            if bridge.active:
                break
            await asyncio.sleep(0.05)
        offset = await bridge.clock_offset()
        key = []
        for i in range(n):
            sent = now_us()
            ack = await bridge.send_image((13, 12, 120, 120), jpeg(Image.new("RGB", (120, 120), (i * 5, 40, 60)), 80))
            key.append((ack.t_done - offset - sent) / 1000)
            await asyncio.sleep(0.03)
        press = []
        for _ in range(n):
            t = time.perf_counter()
            await asyncio.to_thread(send, "POST", "/api/input", {"key": 3, "action": "down"})
            await asyncio.wait_for(bridge.inputs.get(), 5)
            press.append((time.perf_counter() - t) * 1000)
            await asyncio.to_thread(send, "POST", "/api/input", {"key": 3, "action": "up"})
            await asyncio.wait_for(bridge.inputs.get(), 5)
    finally:
        await asyncio.to_thread(send, "PUT", "/api/config", {"controller": cfg["controller"], "host": cfg["host"]})
        await server.stop()
    print(f"key image to USB done {summary(key, 0)}\ninput POST to server event {summary(press, 0)}")
    return {"key_image": quantiles(key), "input_event": quantiles(press)}


def firmware_size() -> dict:
    """Image size and the internal RAM taken before the first line runs (DIRAM: code, data, bss)."""
    build = Path(__file__).resolve().parents[2] / "firmware/build"
    out = subprocess.run(
        [
            "bash",
            "-c",
            f". ~/esp/esp-idf/export.sh >/dev/null && python -m esp_idf_size --format json2 {build}/deckbridge.map",
        ],
        capture_output=True,
        text=True,
    )
    if out.returncode:
        return {}
    diram = next(m["used"] for m in json.loads(out.stdout)["layout"] if m["name"] == "DIRAM")
    return {"app": (build / "deckbridge.bin").stat().st_size, "ram_static": diram}


KINDS = {
    "p50": "ms",
    "p95": "ms",
    "status_p95": "ms",
    "rps": "rate",
    "heap_free": "free",
    "heap_largest": "free",
    "heap_min_free": "free",
    "psram_free": "free",
    "psram_largest": "free",
    "app": "size",
    "ram_static": "size",
    "cpu_busy": "cpu",
    "errors": "count",
    "ws_drops": "count",
}
FLOOR = {"ms": 0.10, "rate": 0.10, "free": 0.01, "size": 0.0, "cpu": 0.0, "count": 0.0}  # relative, before the band
WORSE_IF_HIGHER = {"ms", "size", "cpu", "count"}


def flat(d: dict, prefix: str = "") -> dict:
    out = {}
    for k, v in d.items():
        if isinstance(v, dict):
            out.update(flat(v, f"{prefix}{k}."))
        elif isinstance(v, (int, float)) and k in KINDS:
            out[prefix + k] = v
    return out


def compare(base_file: str, new_file: str) -> int:
    """Worse beyond noise: the gap between the medians is larger than the spread inside either file (or a floor)."""
    base, new = (json.loads(Path(f).read_text())["runs"] for f in (base_file, new_file))
    a, b = [flat(r) for r in base], [flat(r) for r in new]
    worse = 0
    for key in sorted(a[0]):
        va, vb = [r[key] for r in a if key in r], [r[key] for r in b if key in r]
        if not va or not vb:
            continue
        kind = KINDS[key.rsplit(".", 1)[-1]]
        ma, mb = statistics.median(va), statistics.median(vb)
        band = max(max(va) - min(va), max(vb) - min(vb), abs(ma) * FLOOR[kind])
        delta = mb - ma
        bad = (delta > band) if kind in WORSE_IF_HIGHER else (-delta > band)
        worse += bad
        print(f"{'WORSE' if bad else '     '} {key:44} {ma:12.1f} -> {mb:12.1f}  band {band:.1f}")
    print(f"\n{worse} metric(s) worse beyond noise")
    return 1 if worse else 0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("DB_HOST", "192.168.1.214"))
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("-n", type=int, default=200)
    ap.add_argument("--json", help="add this run to a baseline file")
    ap.add_argument("--compare", nargs=2, metavar=("BASE", "NEW"))
    a = ap.parse_args()
    if a.compare:
        raise SystemExit(compare(*a.compare))
    pw = (Path.home() / ".config/deckbridge/admin_pass").read_text().strip()
    auth = "Basic " + base64.b64encode(f"admin:{pw}".encode()).decode()
    probe = Client(a.host, auth)

    def status():
        code, _, body = probe.get("/api/status")
        if code != 200:
            raise SystemExit(f"/api/status answered {code or 'nothing'}: check --host and the password")
        return json.loads(body)

    def mem():
        m = status()["memory"]
        return f"heap {m['heap_free'] // 1024} KB free, lowest ever {m['heap_min_free'] // 1024} KB"

    def profile():
        code, _, body = probe.get("/api/profile")
        if code != 200:
            print(f"  /api/profile answered {code or 'nothing'}")
            return {}
        p = json.loads(body)
        busy = sorted((p.get("cpu_permille") or {}).items(), key=lambda kv: -kv[1])[:6]
        print("  CPU per task (per mille of both cores):", ", ".join(f"{k} {v}" for k, v in busy))
        for r in sorted(p["routes"], key=lambda r: -r["count"] * r["avg_us"])[:6]:
            print(
                f"  {r['method']:4} {r['uri']:16} n={r['count']:5}"
                f" avg={r['avg_us'] / 1000:6.1f} max={r['max_us'] / 1000:6.1f} ms"
            )
        return p.get("cpu_permille") or {}

    run: dict = {}
    profile()
    time.sleep(5)  # nothing but the hosts: the idle CPU of the next profile call
    idle = profile()
    busy = {k: v for k, v in idle.items() if v and not k.startswith("IDLE")}
    run["cpu_idle"] = {"total": {"cpu_busy": 1000 - idle.get("IDLE0", 0) - idle.get("IDLE1", 0)}} | {
        k: {"cpu_busy": v} for k, v in busy.items()
    }
    print("before:", mem())
    run["memory_before"] = status()["memory"]
    run["latency"] = latency(a.host, auth, a.n)
    profile()
    print("\n# Many requests at once")
    run["hammer"] = {}
    for path in ["/api/status", "/api/screen", "/api/screen.jpg"]:
        for threads in (1, 4, 8):
            run["hammer"][f"{path} x{threads}"] = hammer(a.host, auth, path, threads, a.secs / 2)
    profile()
    print("\n# Home Assistant instances")
    run["home_assistant"] = {f"x{count}": home_assistants(a.host, auth, count, a.secs) for count in (1, 3, 5, 8)}
    profile()
    print("\nafter:", mem())
    if not a.json:
        return
    print("\n# Web socket and a drawing server")
    run["ws_connect"] = ws_connect(a.host, auth, 20)
    run["drawing"] = asyncio.run(drawing(a.host, auth, 30))
    s = status()
    run["memory_after"] = s["memory"]
    run["size"] = firmware_size()
    run["firmware"] = s["firmware"]
    run["at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    path = Path(a.json)
    data = json.loads(path.read_text()) if path.exists() else {"runs": []}
    data["runs"].append(run)
    path.write_text(json.dumps(data, indent=1) + "\n")
    print(f"\nadded run {len(data['runs'])} to {path}")


if __name__ == "__main__":
    main()
