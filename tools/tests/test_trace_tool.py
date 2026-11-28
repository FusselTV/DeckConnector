"""The board trace tool offline: the clock fit and the ring to Chrome trace JSON."""

import random

import pytest

from dbtools.trace import EVENT, FRAME_FLOW, POINTS, chrome, decode, fit

pytestmark = pytest.mark.sim


def test_fit_finds_offset_and_drift_through_jittery_pings():
    rng = random.Random(1)
    offset, ppm = 1_795_700_000_000_000, -20
    samples = []
    for i in range(600):
        t1 = 1_795_700_000_000_000 + i * 100_000
        up, down = 300 + rng.expovariate(1 / 2000), 300 + rng.expovariate(1 / 2000)
        t2 = round((t1 + up - offset) / (1 + ppm * 1e-6))
        t3 = t2 + 50
        t4 = round(offset + t3 * (1 + ppm * 1e-6) + down)
        samples.append((t1, t2, t3, t4))
    f = fit(samples)
    board = samples[300][1]
    assert abs(f["a"] + f["b"] * board + board - (offset + board * (1 + ppm * 1e-6))) < 100
    assert abs(f["drift_ppm"] - ppm) < 5  # a 60 s run pins the drift only this well


def test_press_and_its_frame_are_one_flow():
    p = {name: i for i, name in enumerate(POINTS) if name}
    raw = b"".join(
        EVENT.pack(t, p[point], d, aux, ident)
        for t, point, d, aux, ident in [
            (1000, "E1", 0, 0, 77),
            (1010, "L1", 0, 1, 5),
            (1020, "L2", 0, 0x83, 5),
            (1100, "L2_END", 0, 0x83, 5),
            (5000, "B1", 0, 2, 20),
            (5100, "B2", 0, 0x04, 9),
            (5110, "B3", 0, 0x0300, 9),
            (5120, "B4", 0, 10, 0x30300),
            (5200, "B4_END", 0, 10, 0x30300),
            (5300, "B6", 0, 10, 0x30300),
            (8000, "B6_END", 0, 0, 0x30300),
        ]
    )
    out = chrome(decode(raw), {"a": 10_000_000.0, "b": 0.0}, links={5: 9})
    steps = [e for e in out["traceEvents"] if e.get("cat") == "flow" and e["id"] == FRAME_FLOW | 9]
    assert [e["ph"] for e in steps] == ["s", "t", "t", "t", "f"]
    write = next(e for e in out["traceEvents"] if e.get("name") == "USB write image")
    assert (write["ts"], write["dur"], write["args"]["frame"]) == (10_005_300.0, 2700.0, 9)


def test_a_reconnect_shows_its_phases_and_the_wait():
    p = {name: i for i, name in enumerate(POINTS) if name}
    raw = b"".join(
        EVENT.pack(t, p[point], 1, aux, 0)
        for t, point, aux in [
            (1000, "C5", 0),
            (3000, "C0", 0),
            (3500, "C4", 111),
            (7000, "C0", 0),
            (7400, "C1", 0),
            (9000, "C2", 0),
            (9300, "C3", 0),
            (9500, "C4", 0),
            (9800, "C5", 0),
            (10000, "C0", 0),
            (10100, "C1", 0),
            (10400, "C2", 1),
            (10500, "C3", 0),
            (10600, "C4", 0),
        ]
    )
    out = chrome(decode(raw), {"a": 0.0, "b": 0.0})
    got = [(e["name"], e["ts"], e["dur"]) for e in out["traceEvents"] if e.get("ph") == "X"]
    assert sorted(got) == sorted(
        [
            ("away (backoff)", 1000, 2000),
            ("connect failed (errno 111)", 3000, 500),
            ("TCP connect", 7000, 400),
            ("TLS", 7400, 1600),
            ("WebSocket upgrade", 9000, 300),
            ("HELLO", 9300, 200),
            ("connected", 7000, 2500),
            ("away (backoff)", 9800, 200),
            ("TCP connect", 10000, 100),
            ("TLS resumed", 10100, 300),
            ("WebSocket upgrade", 10400, 100),
            ("HELLO", 10500, 100),
            ("connected", 10000, 600),
        ]
    )
