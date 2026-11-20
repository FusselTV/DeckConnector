"""POST /api/sound/stream on the firmware's own web server: the simulator's silent speaker takes as long as the
board's would, so these show timing and that the server keeps answering while a stream plays."""

import base64
import http.client
import json
import math
import os
import socket
import struct
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
    status, body, _took = post_stream(sim, b"\0" * (RATE // 2))
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


def open_stream(sim, pcm: bytes, pause_at: int = -1, pause: float = 0, end: bool = True) -> tuple[dict, float]:
    """Sends like Home Assistant: a Content-Length it never reaches, paced a second ahead, then shuts its side."""
    auth = base64.b64encode(f"admin:{sim.password}".encode()).decode()
    s = socket.create_connection(("127.0.0.1", sim.port), timeout=30)
    start = time.monotonic()
    try:
        s.sendall(
            f"POST /api/sound/stream HTTP/1.1\r\nHost: x\r\nAuthorization: Basic {auth}\r\nX-DeckConnector: 1\r\n"
            f"Content-Length: {2**31 - 1}\r\n\r\n".encode()
        )
        for at in range(0, len(pcm), RATE // 10):
            if at == pause_at:
                time.sleep(pause)
            ahead = at / RATE - (time.monotonic() - start) - 1
            if ahead > 0:
                time.sleep(ahead)
            s.sendall(pcm[at : at + RATE // 10])
        if end:
            s.shutdown(socket.SHUT_WR)
        answer = b""
        while chunk := s.recv(4096):
            answer += chunk
        return json.loads(answer.partition(b"\r\n\r\n")[2]), time.monotonic() - start
    finally:
        s.close()


def cpu_s(pid: int) -> float:
    fields = open(f"/proc/{pid}/stat").read().rpartition(")")[2].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def test_twenty_seconds_of_unknown_length_play_through(start):
    sim = start("plus")
    sim.config(speaker=True)
    pcm = b"".join(struct.pack("<h", int(8000 * math.sin(i / 5))) for i in range(16000 * 20))
    cpu = cpu_s(sim.proc.pid)
    t = threading.Thread(target=lambda: result.update(r=open_stream(sim, pcm)))
    result = {}
    t.start()
    worst = 0.0
    while t.is_alive():
        asked = time.monotonic()
        sim.status()
        worst = max(worst, time.monotonic() - asked)
        time.sleep(1)
    t.join()
    body, took = result["r"]
    used = cpu_s(sim.proc.pid) - cpu
    print(f"\n20 s stream: took {took:.1f} s, sim CPU {used:.2f} s ({100 * used / took:.1f} %), status worst {worst:.3f} s")
    assert body == {"ok": True, "stopped": False}
    assert 19 < took < 22  # the sim's speaker runs a few % fast: 32 ms chunks wait 3 ticks of 10 ms
    assert worst < 0.5, "the server answers while a stream plays"


def test_a_stall_plays_silence_and_goes_on(start):
    sim = start("plus")
    sim.config(speaker=True)
    pcm = b"\1\0" * 16000 * 4
    _body, plain = open_stream(sim, pcm)
    body, took = open_stream(sim, pcm, pause_at=RATE * 2, pause=2)
    assert body == {"ok": True, "stopped": False}
    assert 0.5 < took - plain < 3  # silence while the ring ran dry, then the rest


def test_five_seconds_without_data_end_a_stream(start):
    sim = start("plus")
    sim.config(speaker=True)
    body, took = open_stream(sim, b"\1\0" * 16000, end=False)
    assert body == {"ok": True, "stopped": True}
    assert 5.5 < took < 8


def test_a_reset_ends_a_stream_at_once(start):
    """Home Assistant's stop: a close would let the board play out what it holds."""
    sim = start("plus")
    sim.config(speaker=True)
    s = socket.create_connection(("127.0.0.1", sim.port), timeout=30)
    auth = base64.b64encode(f"admin:{sim.password}".encode()).decode()
    s.sendall(
        f"POST /api/sound/stream HTTP/1.1\r\nHost: x\r\nAuthorization: Basic {auth}\r\nX-DeckConnector: 1\r\n"
        f"Content-Length: {2**31 - 1}\r\n\r\n".encode()
        + b"\1\0" * 16000 * 3
    )
    time.sleep(1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    s.close()
    time.sleep(0.3)
    status, body, _took = post_stream(sim, b"\0" * 3200)
    assert (status, body) == (200, {"ok": True, "stopped": False}), "the next stream plays: the last one is over"
