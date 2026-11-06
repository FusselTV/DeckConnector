import asyncio
import itertools
import json
import statistics
import time

import pytest
from conftest import api, jpeg, newest, status, strip_color

from dbtools.protocol import AckResult

FULL = (0, 0, 800, 480)
KEY0 = (13, 12, 120, 120)
QUARTER = (200, 380, 200, 100)


async def test_hello_and_device(bridges):
    active, standby = bridges
    assert active.hello["proto"] == 1
    assert active.hello["fw"] == status()["firmware"]
    assert active.active and not standby.active
    dev = active.device
    assert dev["present"] and dev["model"] == "Stream Deck +" and dev["tested"]
    kinds = [t["kind"] for t in dev["displays"][0]["targets"]]
    assert kinds.count("key") == 8 and "full" in kinds and "strip" in kinds
    assert dev["touch"][0]["y"] == 380


async def test_image_reaches_the_glass(bridges):
    active, _ = bridges
    for color, channel in (((220, 20, 20), 0), ((20, 40, 230), 2)):
        ack = await active.send_image(FULL, jpeg(color))
        assert ack.result == AckResult.SHOWN
        await asyncio.sleep(0.5)
        seen = await asyncio.to_thread(strip_color)
        assert max(range(3), key=lambda i: seen[i]) == channel, f"camera saw {seen} for {color}"


async def test_rejections(bridges):
    active, standby = bridges
    assert (await active.send_image((1, 1, 50, 50), jpeg("red", (50, 50)))).result == AckResult.REJECTED_RECT
    assert (await active.send_image(FULL, jpeg("red"), display=3)).result == AckResult.REJECTED_RECT
    assert (await standby.send_image(FULL, jpeg("red"))).result == AckResult.REJECTED_ROLE


def noise_jpeg(seed: int) -> bytes:
    """About 100 KB, so the USB side is slower than the 100 Mbit LAN and a queue would build up."""
    import io
    import random

    from PIL import Image

    rnd = random.Random(seed)
    img = Image.frombytes("RGB", (800, 480), rnd.randbytes(800 * 480 * 3))
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=60)
    return buf.getvalue()


async def test_newest_image_wins(bridges):
    active, _ = bridges
    images = await asyncio.to_thread(lambda: [noise_jpeg(i) for i in range(20)])  # seconds of CPU: off the loop
    futures = [await active.post_image(FULL, img) for img in images]
    acks = await asyncio.wait_for(asyncio.gather(*futures), 10)
    results = [a.result for a in acks]
    assert results[-1] == AckResult.SHOWN
    assert set(results) <= {AckResult.SHOWN, AckResult.SUPERSEDED}
    # whether a queue builds depends on the path (a fresh link may run over WLAN): what counts is that no image
    # starts on the deck once a newer one has arrived
    for older, newer in itertools.pairwise(acks):
        if older.result == AckResult.SHOWN:
            assert older.t_start <= newer.t_recv, f"image {older.seq} was drawn after {newer.seq} arrived"


async def test_partial_strip_and_keys(bridges):
    active, _ = bridges
    for rect, size in ((QUARTER, (200, 100)), (KEY0, (120, 120)), ((0, 380, 800, 100), (800, 100))):
        assert (await active.send_image(rect, jpeg("green", size))).result == AckResult.SHOWN


async def test_clock_sync(bridges):
    active, _ = bridges
    a = await active.clock_offset(10)
    b = await active.clock_offset(10)
    assert abs(a - b) < 2000, "two estimates of the clock offset should agree within 2 ms"


async def test_latency_lan(bridges):
    """Server send to USB done, measured on one clock. Prints the numbers for the report."""
    active, _ = bridges
    offset = await active.clock_offset()
    results = {}
    for name, rect, size in (("key", KEY0, (120, 120)), ("quarter", QUARTER, (200, 100)), ("full", FULL, (800, 480))):
        data, times = jpeg((40, 90, 160), size, 85), []
        for _ in range(40):
            sent = time.monotonic_ns() // 1000
            ack = await active.send_image(rect, data)
            times.append((ack.t_done - offset - sent) / 1000)
            await asyncio.sleep(0.02)
        results[name] = (statistics.median(times), sorted(times)[int(len(times) * 0.95)])
    print("latency ms (median, p95):", json.dumps(results))
    assert results["key"][1] < 5 and results["full"][1] < 8


async def test_brightness_command(bridges):
    active, _ = bridges
    await active.send_json({"t": "brightness", "pct": 40})
    await active.send_json({"t": "brightness", "pct": 80})
    assert (await active.send_image(KEY0, jpeg("white", (120, 120)))).result == AckResult.SHOWN


async def test_failover_and_return(servers, bridges):
    first, _ = servers
    _, b = bridges
    b.role_changed.clear()
    t0 = time.monotonic()
    await first.stop()
    await asyncio.wait_for(b.role_changed.wait(), 30)
    took = time.monotonic() - t0
    print(f"failover to the standby server in {took * 1000:.0f} ms")
    assert b.active
    assert (await b.send_image(FULL, jpeg((0, 150, 0)))).result == AckResult.SHOWN

    while not first.connected.empty():
        first.connected.get_nowait()
    await first.start()
    back = await first.wait_bridge(30)
    for _ in range(100):
        if back.active and not b.active:
            break
        await asyncio.sleep(0.05)
    assert back.active and not b.active, "the first server is preferred again once it is back"
    assert (await b.send_image(FULL, jpeg("red"))).result == AckResult.REJECTED_ROLE


def test_web_api_needs_login():
    assert api("GET", "/api/status", auth=False)[0] == 401
    assert api("GET", "/api/config")[0] == 200
    assert "admin_pass" not in json.loads(api("GET", "/api/config")[1])


@pytest.mark.parametrize(
    "body, error",
    [
        ({"lan": {"dhcp": False, "ip": "300.1.1.1", "mask": "255.255.255.0"}}, "invalid IPv4"),
        ({"lan": {"dhcp": False, "ip": "", "mask": ""}}, "fixed address needs"),
        ({"admin_pass": "abc"}, "at least 6"),
        ({"servers": ["http://x"]}, "ws://"),
        ({"brightness": 101}, "0 to 100"),
        ({"controller": "fast"}, "server, app or companion"),
    ],
)
def test_web_api_rejects_bad_config(body, error):
    code, reply = api("PUT", "/api/config", json.dumps(body).encode())
    assert code == 400 and error in json.loads(reply)["error"]


def test_web_api_brightness_applies_without_restart():
    before = status()["uptime_s"]
    code, reply = api("PUT", "/api/config", json.dumps({"brightness": 75}).encode())
    assert code == 200 and json.loads(reply)["restart"] is False
    assert status()["uptime_s"] >= before


def test_web_api_unknown_endpoint_answers_json():
    code, reply = api("GET", "/api/nope")
    assert code == 404 and json.loads(reply)["ok"] is False


async def test_stalled_frame_ends_the_session(servers):
    """A frame that stops halfway (dead switch port) must not hang the link: the bridge reconnects."""
    first, _ = servers
    active = await newest(first)
    while not first.connected.empty():  # earlier connects, the active one among them
        first.connected.get_nowait()
    size = 100_000
    header = bytes([0x82, 127]) + size.to_bytes(8, "big")
    active.ws.transport.write(header + bytes([0x01]) + bytes(1000))
    t0 = time.monotonic()
    back = await first.wait_bridge(40)
    took = time.monotonic() - t0
    print(f"reconnected after a stalled frame in {took:.1f} s")
    assert back is not active and took < 10
