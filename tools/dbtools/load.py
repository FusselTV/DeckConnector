"""Load test for the HTTP API: latency per endpoint, throughput, and several Home Assistant instances at once.

uv run python -m dbtools.load [--host 192.168.1.214] [--secs 20]
"""

import argparse
import base64
import http.client
import json
import os
import statistics
import threading
import time
from collections import Counter
from pathlib import Path

from websockets.sync.client import connect

ENDPOINTS = ["/api/status", "/api/config", "/api/screen", "/api/screen.jpg", "/"]
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


def summary(ms: list[float], errors: int) -> str:
    if not ms:
        return f"no answers, {errors} errors"
    q = statistics.quantiles(ms, n=100) if len(ms) > 1 else [ms[0]] * 99
    return f"n={len(ms):4} p50={q[49]:6.1f} p95={q[94]:6.1f} max={max(ms):6.1f} ms  errors={errors}"


def latency(host: str, auth: str, n: int) -> None:
    print("\n# One client, one request after the other")
    c = Client(host, auth)
    for path in ENDPOINTS:
        ms, errors = [], 0
        for _ in range(n // 4 if path.endswith(".jpg") else n):
            code, t, _ = c.get(path)
            if code == 200:
                ms.append(t)
            else:
                errors += 1
        print(f"{path:16} {summary(ms, errors)}  {report_failures()}")


def hammer(host: str, auth: str, path: str, threads: int, secs: float) -> None:
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


def home_assistants(host: str, auth: str, count: int, secs: float) -> None:
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("DB_HOST", "192.168.1.214"))
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("-n", type=int, default=200)
    a = ap.parse_args()
    pw = (Path.home() / ".config/deckbridge/admin_pass").read_text().strip()
    auth = "Basic " + base64.b64encode(f"admin:{pw}".encode()).decode()
    probe = Client(a.host, auth)

    def mem():
        code, _, body = probe.get("/api/status")
        if code != 200:
            raise SystemExit(f"/api/status answered {code or 'nothing'}: check --host and the password")
        m = json.loads(body)["memory"]
        return f"heap {m['heap_free'] // 1024} KB free, lowest ever {m['heap_min_free'] // 1024} KB"

    def profile():
        code, _, body = probe.get("/api/profile")
        if code != 200:
            print(f"  /api/profile answered {code or 'nothing'}")
            return
        p = json.loads(body)
        busy = sorted((p.get("cpu_permille") or {}).items(), key=lambda kv: -kv[1])[:6]
        print("  CPU per task (per mille of both cores):", ", ".join(f"{k} {v}" for k, v in busy))
        for r in sorted(p["routes"], key=lambda r: -r["count"] * r["avg_us"])[:6]:
            print(
                f"  {r['method']:4} {r['uri']:16} n={r['count']:5}"
                f" avg={r['avg_us'] / 1000:6.1f} max={r['max_us'] / 1000:6.1f} ms"
            )

    print("before:", mem())
    profile()
    latency(a.host, auth, a.n)
    profile()
    print("\n# Many requests at once")
    for path in ["/api/status", "/api/screen", "/api/screen.jpg"]:
        for threads in (1, 4, 8):
            hammer(a.host, auth, path, threads, a.secs / 2)
    profile()
    print("\n# Home Assistant instances")
    for count in (1, 3, 5, 8):
        home_assistants(a.host, auth, count, a.secs)
    profile()
    print("\nafter:", mem())


if __name__ == "__main__":
    main()
