"""POST /api/sound/stream on the firmware's own web server: the simulator's silent speaker takes as long as the
board's would, so these show timing and that the server keeps answering while a stream plays."""

import base64
import http.client
import json
import threading
import time

RATE = 32000  # bytes per second: 16 kHz, 16 bit, mono


def post_stream(sim, pcm: bytes, c: http.client.HTTPConnection | None = None) -> tuple[int, dict, float]:
    conn = c or http.client.HTTPConnection("127.0.0.1", sim.port, timeout=30)
    auth = base64.b64encode(f"admin:{sim.password}".encode()).decode()
    start = time.monotonic()
    try:
        conn.request("POST", "/api/sound/stream", pcm, {"Authorization": f"Basic {auth}", "X-DeckConnector": "1"})
        r = conn.getresponse()
        return r.status, json.loads(r.read()), time.monotonic() - start
    finally:
        if not c:
            conn.close()


def test_a_stream_plays_in_real_time_and_the_server_keeps_answering(start):
    sim = start("plus")
    status, body, took = post_stream(sim, b"\0" * RATE)
    assert (status, body["error"]) == (409, "speaker: the settings say none is connected")
    assert took < 1, "refused at once, without reading the body first"
    sim.config(speaker=True)

    result = {}
    t = threading.Thread(target=lambda: result.update(r=post_stream(sim, b"\1\0" * RATE)))  # 2 s
    t.start()
    time.sleep(0.5)
    asked = time.monotonic()
    assert sim.status()["speaker"]
    assert time.monotonic() - asked < 0.5, "the server answers while a stream plays"
    t.join()
    status, body, took = result["r"]
    assert (status, body) == (200, {"ok": True, "stopped": False})
    assert 1.9 < took < 3


def test_a_newer_stream_ends_the_one_playing(start):
    sim = start("plus")
    sim.config(speaker=True)
    result = {}
    t = threading.Thread(target=lambda: result.update(first=post_stream(sim, b"\0" * RATE * 10)))
    t.start()
    time.sleep(1)
    status, body, took = post_stream(sim, b"\0" * (RATE // 2))
    t.join()
    assert (status, body) == (200, {"ok": True, "stopped": False})
    first_status, first, first_took = result["first"]
    assert (first_status, first) == (200, {"ok": True, "stopped": True})
    assert first_took < 3


def test_a_client_that_keeps_connections_does_not_reuse_a_streams(start):
    """The board closes the connection after a stream; without saying so a client sent its next request into it."""
    sim = start("plus")
    sim.config(speaker=True)
    c = http.client.HTTPConnection("127.0.0.1", sim.port, timeout=30)
    for _ in range(2):
        assert post_stream(sim, b"\0" * 3200, c)[:2] == (200, {"ok": True, "stopped": False})
    c.close()


def test_turning_the_speaker_off_ends_the_stream(start):
    sim = start("plus")
    sim.config(speaker=True)
    result = {}
    t = threading.Thread(target=lambda: result.update(r=post_stream(sim, b"\0" * RATE * 10)))
    t.start()
    time.sleep(1)
    sim.config(speaker=False)
    t.join()
    status, body, took = result["r"]
    assert (status, body) == (200, {"ok": True, "stopped": True})
    assert took < 3
