import asyncio
import json
import statistics
import time

import pytest
from conftest import api, jpeg, newest, paint, status, strip_color

from dbtools import protocol as p


async def test_hello_and_decks(bridges):
    active, standby = bridges
    assert active.hello["proto"] == 2
    assert active.hello["firmware"] == status()["firmware"]
    assert not active.pairing and not standby.pairing, "both are paired"
    deck = active.decks[0]
    assert deck["name"] == "Stream Deck +" and deck["keys"] == 8 and deck["encoders"] == 4
    kinds = [t["kind"] for t in deck["targets"]]
    assert kinds.count("key") == 8 and "full" in kinds and "strip" in kinds
    assert deck["touch"][0]["y"] == active.target(0, "strip")["y"] == 380


async def test_image_reaches_the_glass(bridges):
    active, _ = bridges
    for color, channel in (((220, 20, 20), 0), ((20, 40, 230), 2)):
        assert (await paint(active, color)).us > 0, active.errors
        await asyncio.sleep(0.5)
        seen = await asyncio.to_thread(strip_color)
        assert max(range(3), key=lambda i: seen[i]) == channel, f"camera saw {seen} for {color}"


async def test_rejections(bridges):
    active, standby = bridges
    key = active.target(0)
    await active.put(90, jpeg("red", (50, 50)), 50, 50)
    assert (await active.show(0, [(key["id"], 90)])).us == 0 and active.errors[-1]["code"] == "bad_size"
    assert (await active.show(3, [(key["id"], 90)])).us == 0 and active.errors[-1]["code"] == "no_deck"
    assert (await paint(standby, "red")).us == 0 and standby.errors[-1]["code"] == "standby"


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


async def test_newest_stream_wins(bridges):
    active, _ = bridges
    full = active.target(0, "full")["id"]
    images = await asyncio.to_thread(lambda: [noise_jpeg(i) for i in range(20)])  # seconds of CPU: off the loop
    futures = [await active.stream(0, full, img) for img in images]
    shown = await asyncio.wait_for(asyncio.gather(*futures), 10)
    assert shown[-1].us > 0, "the newest one is shown"
    assert len({s.id for s in shown}) == len(images), "every STREAM gets its own SHOWN"


async def test_strip_and_keys(bridges):
    active, _ = bridges
    for kind in ("key", "strip"):
        assert (await paint(active, "green", kind)).us > 0, active.errors


async def test_latency_lan(bridges):
    """Host send to SHOWN, and the bridge's own time to glass. Prints the numbers for the report."""
    active, _ = bridges
    results = {}
    for kind in ("key", "strip", "full"):
        t = active.target(0, kind)
        data, times, glass = jpeg((40, 90, 160), (t["w"], t["h"]), 85), [], []
        for _ in range(40):
            sent = time.perf_counter()
            shown = await active.draw(0, t, data, t["id"])
            times.append((time.perf_counter() - sent) * 1000)
            glass.append(shown.us / 1000)
            await asyncio.sleep(0.02)
        results[kind] = (statistics.median(times), sorted(times)[int(len(times) * 0.95)], statistics.median(glass))
    print("latency ms (median, p95, median SHOWN.us):", json.dumps(results))
    if status()["counters"]["usb_speed"] == "full":
        pytest.skip(f"the USB port runs at full speed, about 64 KB/s: {results}")
    assert results["key"][1] < 5 and results["full"][1] < 8


async def test_brightness_command(bridges):
    active, _ = bridges
    await active.send(p.brightness(40))
    await active.send(p.brightness(80))
    assert (await paint(active, "white", "key")).us > 0
    for _ in range(20):
        if active.states.get(0, {}).get("brightness") == 80:
            break
        await asyncio.sleep(0.1)
    assert active.states[0]["brightness"] == 80, active.states


async def test_the_standby_server_takes_over_and_gives_back(servers, bridges):
    first, _ = servers
    _, b = bridges
    t0 = time.monotonic()
    await first.stop()
    await b.role(True)
    shown = await paint(b, (0, 150, 0))
    print(f"the second server drew {(time.monotonic() - t0) * 1000:.0f} ms after the first one went")
    assert shown.us > 0, b.errors

    while not first.connected.empty():
        first.connected.get_nowait()
    await first.start()
    back = await first.wait_bridge(30)
    await back.role(True)  # the last host that asks draws
    assert (await paint(back, (0, 0, 150))).us > 0, back.errors
    assert (await paint(b, "red")).us == 0 and b.errors[-1]["code"] == "standby"


def test_web_api_needs_login():
    assert api("GET", "/api/status", auth=False)[0] == 401
    assert api("GET", "/api/config")[0] == 200
    assert "admin_pass" not in json.loads(api("GET", "/api/config")[1])


@pytest.mark.parametrize(
    "body, error",
    [
        ({"lan": {"dhcp": False, "ip": "300.1.1.1", "mask": "255.255.255.0"}}, "invalid IPv4"),
        ({"lan": {"dhcp": False, "ip": "", "mask": ""}}, "fixed address needs"),
        ({"admin_pass": "abc"}, "6 to 23"),
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


@pytest.mark.sim
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
