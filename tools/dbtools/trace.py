"""Board profiling: the trace ring, the clock fit and Chrome trace JSON (opens in https://ui.perfetto.dev).

uv run python -m dbtools.trace press [--n 20] [--server srv.json] -o run.json
    pairs a host on this machine, records while it presses a key (through the input gate) and draws the answer,
    and writes the board's hops, this host's and the clock error into one timeline
uv run python -m dbtools.trace record --secs 10 [--server srv.json] -o run.json
    records whatever the bridge does meanwhile (presses by hand, the production host drawing)
uv run python -m dbtools.trace bench    the board's microbenchmarks

Board times are the board's esp_timer; `{"t":"clock"}` pings over /api/ws give NTP-style samples, and the
offset is fitted with its drift over the run (lowest-RTT tenth). All times in the output are Unix µs, like the
Deck Assistant server's trace. A server trace given with --server is merged: its slices that carry `seq`
(INPUT seq) or `frame` (FRAME id) in args lend their flow id to the board's events of the same press or frame.
"""

import argparse
import asyncio
import json
import re
import struct
import threading
import time
from collections import defaultdict
from collections.abc import Callable
from pathlib import Path

from websockets.sync.client import connect

from .load import Client, basic_auth, drawing_for
from .safety import real_input_target
from .site import HOST

# db_trace.h: db_tp_t and db_tc_t, in order
POINTS = [
    None,
    "U1",
    "E1",
    "E2",
    "L1",
    "L2",
    "L2_END",
    "B1",
    "B2",
    "B3",
    "B4",
    "B4_END",
    "B5",
    "B6",
    "B6_END",
    "B7",
    "B7_END",
    "B8",
    "COUNTER",
    "BENCH",
    "C0",
    "C1",
    "C2",
    "C3",
    "C4",
    "C5",
]
COUNTERS = [
    "heap_free",
    "heap_min",
    "heap_largest",
    "psram_free",
    "psram_min",
    "psram_largest",
    "rssi",
    "link_frames",
    "link_idle",
    "tx_msgs",
    "event_wakes",
    "writer_wakes",
    "writer_ticks",
]
RATES = set(COUNTERS[7:])  # cumulative: shown per second
EVENT = struct.Struct("<QBBHI")
MSG = {0x02: "PUT", 0x04: "FRAME", 0x05: "STREAM", 0x81: "SHOWN", 0x82: "CREDIT", 0x83: "INPUT", 0x87: "ERROR"}
BOARD_PID, HOST_PID = 5, 6  # 1 server, 2 server's board estimate, 3 browser, 4 desktop companion
INPUT_FLOW, FRAME_FLOW = 1 << 40, 2 << 40


def unix_us() -> int:
    return time.time_ns() // 1000


class Clock:
    """Pings `{"t":"clock"}` at `hz` on its own thread while open; `fit()` maps board µs to Unix µs."""

    def __init__(self, host: str, auth: str, hz: float = 10):
        self.samples: list[tuple[int, int, int, int]] = []
        self._stop = threading.Event()
        self._ws = connect(f"ws://{host}/api/ws", additional_headers={"Authorization": auth}, open_timeout=10)
        self._thread = threading.Thread(target=self._run, args=(1 / hz,), daemon=True)
        self._thread.start()

    def _run(self, every: float) -> None:
        while not self._stop.is_set():
            t1 = unix_us()
            self._ws.send(json.dumps({"t": "clock", "t1": t1}))
            end = time.monotonic() + 1
            while time.monotonic() < end:
                try:
                    m = json.loads(self._ws.recv(timeout=end - time.monotonic()))
                except TimeoutError:
                    break
                if m.get("t") == "clock" and m.get("t1") == t1:
                    self.samples.append((t1, m["t2"], m["t3"], unix_us()))
                    break
            self._stop.wait(every)

    def close(self) -> None:
        self._stop.set()
        self._thread.join()
        self._ws.close()

    def fit(self) -> dict:
        return fit(self.samples)


def fit(samples: list[tuple[int, int, int, int]]) -> dict:
    """offset(board) = a + b * board, least squares over the lowest-RTT tenth (at least 5 samples)."""
    rows = sorted(((t4 - t1) - (t3 - t2), t2, ((t1 - t2) + (t4 - t3)) / 2) for t1, t2, t3, t4 in samples)
    if len(rows) < 5:
        raise SystemExit(f"only {len(rows)} clock samples")
    best = rows[: max(5, len(rows) // 10)]
    xs, ys = [r[1] for r in best], [r[2] for r in best]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys, strict=True)) / sxx if sxx else 0.0
    a = my - b * mx
    resid = [y - (a + b * x) for x, y in zip(xs, ys, strict=True)]
    rtts = [r[0] for r in rows]
    return {
        "a": a,
        "b": b,
        "samples": len(rows),
        "used": len(best),
        "rtt_p10_us": rtts[len(rtts) // 10],
        "rtt_p50_us": rtts[len(rtts) // 2],
        "residual_us": (sum(r * r for r in resid) / len(resid)) ** 0.5,
        "drift_ppm": b * 1e6,
    }


def decode(raw: bytes) -> list[tuple[int, str, int, int, int]]:
    """(board µs, point, deck or link, aux, id) per event."""
    out = []
    for t, p, d, aux, ident in EVENT.iter_unpack(raw):
        out.append((t, POINTS[p] if p < len(POINTS) else f"P{p}", d, aux, ident))
    return out


def chrome(events: list, clock: dict, host: list | None = None, links: dict | None = None) -> dict:
    """Board events as Chrome trace JSON in Unix µs. `host`: this host's slices, `(flow, slice)` pairs. `links`
    maps INPUT seq to the FRAME id the host answered it with, so press and picture are one flow."""
    a, b = clock["a"], clock["b"]
    links = links or {}
    tids = {"usb_client": 1, "deck_events": 2, "link": 3, "link_tx": 4, "deck_writer": 5, "counters": 6, "connect": 7}
    out: list[dict] = [{"ph": "M", "pid": BOARD_PID, "name": "process_name", "args": {"name": "DeckConnector board"}}]
    out += [
        {"ph": "M", "pid": BOARD_PID, "tid": t, "name": "thread_name", "args": {"name": n}} for n, t in tids.items()
    ]
    flows: dict[int, list[tuple[float, int, int]]] = defaultdict(list)

    def add(flow: int, e: dict) -> None:
        out.append(e)
        if flow:
            flows[flow].append((e["ts"], e["pid"], e["tid"]))

    def slice_(name: str, tid: str, t0: int, t1: int, flow: int = 0, **args) -> None:
        add(
            flow,
            {
                "ph": "X",
                "pid": BOARD_PID,
                "tid": tids[tid],
                "name": name,
                "ts": t0 + a + b * t0,
                "dur": max((t1 - t0) * (1 + b), 1.0),
                "args": args,
            },
        )

    def input_flow(seq: int) -> int:
        return FRAME_FLOW | links[seq] if seq in links else INPUT_FLOW | seq

    def frame_flow(image_seq: int) -> int:
        f = frame_of.get(image_seq & 0xFFFF)
        return FRAME_FLOW | f if f is not None else 0

    frame_of: dict[int, int] = {}  # low 16 bits of an image seq (gen << 8 | flight slot) -> FRAME id, from B3
    u1: dict[int, int] = {}
    e1: tuple[int, int, int | None] | None = None  # time, USB time id, U1 time
    begun: dict[tuple, int] = {}
    l2: dict[int, tuple[int, int]] = {}
    counts: dict[str, tuple[int, int]] = {}
    high = {"deck_events": 0, "link_tx": 0}
    conn: dict[int, dict] = {}  # per link: start and last connect point, end of the last session
    phase = {"C1": "TCP connect", "C2": "TLS", "C3": "WebSocket upgrade", "C4": "HELLO"}
    for t, p, d, aux, ident in events:
        if p == "U1":
            u1[ident] = t
        elif p == "E1":
            high["deck_events"] = max(high["deck_events"], aux)
            e1 = (t, ident, u1.pop(ident, None))
        elif p == "L1" and e1:
            high["link_tx"] = max(high["link_tx"], aux)
            if e1[2] is not None:
                slice_("USB done to event task", "deck_events", e1[2], e1[0], input_flow(ident))
            slice_(f"INPUT {ident} parsed and queued", "deck_events", e1[0], t, input_flow(ident), seq=ident, queue=aux)
        elif p == "L2":
            l2[d] = (t, ident)
        elif p == "L2_END" and d in l2:
            t0, i0 = l2.pop(d)
            flow = input_flow(i0) if aux == 0x83 else FRAME_FLOW | i0 if aux == 0x81 else 0
            slice_(f"send {MSG.get(aux, hex(aux))} {i0}", "link_tx", t0, t, flow, id=i0)
        elif p == "B1":
            begun["B1", d] = t
        elif p == "B2" and ("B1", d) in begun:
            flow = FRAME_FLOW | ident if aux in (0x04, 0x05) else 0
            slice_(f"read {MSG.get(aux, hex(aux))} {ident}", "link", begun.pop(("B1", d)), t, flow, id=ident)
        elif p == "B3":
            frame_of[aux] = ident
        elif p in ("B4", "B6", "B7"):
            begun[p, d, ident] = t
        elif p == "B4_END" and ("B4", d, ident) in begun:
            slice_("copy cached image", "link", begun.pop(("B4", d, ident)), t, frame_flow(ident), bytes=aux * 16)
        elif p == "B6_END" and ("B6", d, ident) in begun:
            f = frame_of.get(ident & 0xFFFF)
            slice_(
                "USB write image", "deck_writer", begun.pop(("B6", d, ident)), t, frame_flow(ident), frame=f, failed=aux
            )
        elif p == "B7_END" and ("B7", d, 0) in begun:
            slice_("report", "usb_client", begun.pop(("B7", d, 0)), t, bytes=aux)
        elif p == "B8":
            out.append(
                {
                    "ph": "i",
                    "s": "t",
                    "pid": BOARD_PID,
                    "tid": tids["deck_writer"],
                    "name": f"SHOWN {ident}",
                    "ts": t + a + b * t,
                    "args": {"stream": aux},
                }
            )
        elif p == "C0":
            c = conn.setdefault(d, {})
            if "down" in c:
                slice_("away (backoff)", "connect", c.pop("down"), t, link=d)
            c.update(start=t, last=t, wlan=aux)
        elif p in phase and "last" in conn.get(d, {}):
            c = conn[d]
            if p != "C4" or aux == 0:
                slice_(phase[p], "connect", c["last"], t, link=d)
                c["last"] = t
            if p == "C4":
                name = "connected" if aux == 0 else f"connect failed (errno {aux})"
                slice_(name, "connect", c.pop("start"), t, link=d, wlan=c["wlan"])
                del c["last"]
        elif p == "C5":
            conn.setdefault(d, {})["down"] = t
        elif p == "COUNTER":
            name = COUNTERS[d] if d < len(COUNTERS) else f"c{d}"
            value: float = ident - 128 if name == "rssi" else ident
            if name in RATES:
                prev, counts[name] = counts.get(name), (t, ident)
                if not prev:
                    continue
                value = (ident - prev[1]) * 1e6 / max(t - prev[0], 1)
            out.append(
                {
                    "ph": "C",
                    "pid": BOARD_PID,
                    "tid": tids["counters"],
                    "name": name,
                    "ts": t + a + b * t,
                    "args": {name: value},
                }
            )
    for flow, e in host or []:
        add(flow, e)
    for ident, steps in flows.items():
        steps.sort()
        for i, (ts, pid, tid) in enumerate(steps if len(steps) > 1 else []):
            ph = "s" if i == 0 else "f" if i == len(steps) - 1 else "t"
            out.append(
                {
                    "ph": ph,
                    "bp": "e",
                    "id": ident,
                    "cat": "flow",
                    "name": "press",
                    "pid": pid,
                    "tid": tid,
                    "ts": ts + 0.5,
                }
            )
    return {
        "traceEvents": out,
        "displayTimeUnit": "ms",
        "metadata": {"clock": clock, "queue_high_water": high, "board_events": len(events)},
    }


def merge(board: dict, server: dict) -> dict:
    """The server's events next to the board's; board flows take the server's flow id where `seq` or `frame`
    names the same press or frame."""
    s_events = server["traceEvents"] if isinstance(server, dict) else server
    to_server: dict[int, int] = {}
    for e in s_events:
        args, flow = e.get("args", {}), e.get("args", {}).get("flow")
        if flow and "seq" in args:
            to_server[INPUT_FLOW | args["seq"]] = flow
        if flow and "frame" in args:
            to_server[FRAME_FLOW | args["frame"]] = flow
    for e in board["traceEvents"]:
        if e.get("cat") == "flow" and e["id"] in to_server:
            e["id"] = to_server[e["id"]]
    board["traceEvents"] += s_events
    board["metadata"]["server"] = server.get("metadata", {}) if isinstance(server, dict) else {}
    return board


def api(c: Client, method: str, path: str, body: dict | None = None):
    status, _, data = c.request(method, path, body)
    if status != 200:
        raise SystemExit(f"{method} {path}: {status} {data[:200]!r}")
    return data


def fetch(c: Client) -> list:
    return decode(api(c, "GET", "/api/trace?events"))


def summary(trace: dict) -> str:
    clock = trace["metadata"]["clock"]
    lines = [
        f"clock: {clock['samples']} pings, RTT p10 {clock['rtt_p10_us'] / 1000:.2f} ms, "
        f"fit residual {clock['residual_us']:.0f} µs, drift {clock['drift_ppm']:+.1f} ppm"
    ]
    by: dict[str, list[float]] = defaultdict(list)
    for e in trace["traceEvents"]:
        if e.get("ph") == "X" and e.get("pid") in (BOARD_PID, HOST_PID):
            by[re.sub(r" \d+", " #", e["name"])].append(e["dur"])
    for name, durs in sorted(by.items()):
        durs.sort()
        lines.append(f"  {name:32s} n={len(durs):4d}  p50 {durs[len(durs) // 2]:8.0f} µs  max {durs[-1]:8.0f} µs")
    return "\n".join(lines)


async def press(
    c: Client, auth: str, n: int, port: int, pair_code: Callable[[], str] | None = None
) -> tuple[list, dict, list, dict]:
    """n presses through the input gate; each INPUT is answered with a new key picture, as a host would."""
    from PIL import Image

    from .bench import jpeg
    from .server import Server

    server = await Server(port, "trace", ping=False, pair_code=pair_code).start()
    host: list[tuple[int, dict]] = [
        (0, {"ph": "M", "pid": HOST_PID, "name": "process_name", "args": {"name": "test host (dbtools)"}})
    ]
    links: dict[int, int] = {}
    try:
        async with drawing_for(c, server) as bridge:
            t = bridge.target(0)
            await bridge.draw(0, t, jpeg(Image.new("RGB", (t["w"], t["h"]), (0, 0, 0)), 80), t["id"])
            me = real_input_target(json.loads((await asyncio.to_thread(c.get, "/api/status"))[2]))
            api(c, "POST", "/api/trace", {"on": True})
            clock = await asyncio.to_thread(Clock, c.host, auth)
            await asyncio.sleep(2)
            for i in range(n):
                for action in ("down", "up"):
                    await asyncio.to_thread(c.send, "POST", "/api/input", {"key": 3, "action": action, "real": me})
                    ev = await asyncio.wait_for(bridge.inputs.get(), 5)
                    if action == "up":
                        continue
                    got = unix_us()
                    img = jpeg(Image.new("RGB", (t["w"], t["h"]), ((i * 37) % 256, 90, 160)), 80)
                    await bridge.put(t["id"], img, t["w"], t["h"])
                    fut = await bridge.frame(0, [(t["id"], t["id"])])
                    sent, frame_id = unix_us(), bridge.last_frame
                    await asyncio.wait_for(fut, 5)
                    shown = unix_us()
                    links[ev.seq] = frame_id
                    flow = FRAME_FLOW | frame_id
                    host += [
                        (
                            flow,
                            {
                                "ph": "X",
                                "pid": HOST_PID,
                                "tid": 1,
                                "name": f"INPUT {ev.seq} to PUT+FRAME",
                                "ts": got,
                                "dur": max(sent - got, 1),
                            },
                        ),
                        (
                            flow,
                            {
                                "ph": "X",
                                "pid": HOST_PID,
                                "tid": 1,
                                "name": f"FRAME {frame_id} to SHOWN",
                                "ts": sent,
                                "dur": max(shown - sent, 1),
                            },
                        ),
                    ]
                await asyncio.sleep(0.25)
            await asyncio.sleep(1.5)  # one more counter sample
            await asyncio.to_thread(clock.close)
            api(c, "POST", "/api/trace", {"on": False})
            events = fetch(c)
    finally:
        await server.stop()
    return events, clock.fit(), host, links


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("press", "record", "bench"))
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--port", type=int, default=8779, help="for the test host of press")
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--server", type=Path, help="a Deck Assistant server trace to merge")
    ap.add_argument("-o", "--out", type=Path, default=Path("board-trace.json"))
    a = ap.parse_args()
    auth = basic_auth()
    c = Client(a.host, auth)
    if a.cmd == "bench":
        print(json.dumps(json.loads(api(c, "POST", "/api/bench")), indent=1))
        return
    host: list = []
    links: dict = {}
    if a.cmd == "press":
        events, clock, host, links = asyncio.run(press(c, auth, a.n, a.port))
    else:
        api(c, "POST", "/api/trace", {"on": True})
        clk = Clock(a.host, auth)
        time.sleep(a.secs)
        clk.close()
        api(c, "POST", "/api/trace", {"on": False})
        events, clock = fetch(c), clk.fit()
    state = json.loads(api(c, "GET", "/api/trace"))
    trace = chrome(events, clock, host, links)
    trace["metadata"]["wifi_ps"] = state["wifi_ps"]
    if a.server:
        trace = merge(trace, json.loads(a.server.read_text()))
    a.out.write_text(json.dumps(trace))
    print(f"{len(events)} board events, Wi-Fi power save on the WLAN chip: {state['wifi_ps']}, wrote {a.out}")
    print(summary(trace))


if __name__ == "__main__":
    main()
