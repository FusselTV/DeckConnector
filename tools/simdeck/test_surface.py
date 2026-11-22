"""The surface protocol v2 per model, against dbtools.server: pairing, the cache, frames, streams, the fallback
picture, two hosts and the input."""

import asyncio
import io
import json
import time

import pytest
from conftest import DISPLAY, dismiss_prompt, drawn_since, pictures, shot
from PIL import Image

from dbtools import protocol as p
from dbtools.server import Bridge, Server

PORT = 18990


def solid(w: int, h: int, color: tuple[int, int, int], fmt: str = "jpeg") -> bytes:
    buf = io.BytesIO()
    Image.new("RGB", (w, h), color).save(buf, fmt.upper())
    return buf.getvalue()


def near(img: Image.Image, color: tuple[int, int, int]) -> bool:
    got = img.convert("RGB").resize((1, 1)).getpixel((0, 0))
    return all(abs(a - b) < 60 for a, b in zip(got, color, strict=True))


async def paired_bridge(start, srv: Server, *models: str) -> tuple[object, Bridge]:
    """A simulator whose first link points at `srv`, paired, active, its decks known."""
    sim = start(*models)
    if models != ("pedal",):
        await asyncio.to_thread(dismiss_prompt, sim)
    url = f"ws://127.0.0.1:{srv.port}/surface"
    await asyncio.to_thread(sim.config, servers=[url], controller="server", setup_done=True)
    bridge = await srv.wait_bridge(20)
    await bridge.role(True)
    assert await asyncio.to_thread(sim.wait, lambda: all(d["writer"] == "server1" for d in sim.status()["decks"]), 5), (
        sim.status()["decks"]
    )
    return sim, bridge


async def test_pairing_keeps_the_token_and_connects_with_it(start):
    srv = await Server(PORT, auto_pair=False).start()
    try:
        sim = start("plus")
        await asyncio.to_thread(dismiss_prompt, sim)
        before = await asyncio.to_thread(pictures, sim, 0)
        await asyncio.to_thread(
            sim.config, servers=[f"ws://127.0.0.1:{PORT}/surface"], controller="server", setup_done=True
        )
        first = await srv.wait_bridge(15, paired=False)
        code = first.pair_code
        assert code and len(code) == 6 and code.isdigit(), first.hello
        assert await asyncio.to_thread(sim.wait, lambda: drawn_since(sim, 0, before), 5), "no pairing page"
        await asyncio.to_thread(shot, sim, 0, "plus-pairing")
        await first.role(True)
        await asyncio.sleep(0.3)
        assert [e["code"] for e in first.errors] == ["unpaired"]
        token = await srv.pair(first)
        again = await srv.wait_bridge(10)
        assert again is not first and "pair_code" not in again.hello
        assert again.ws.request.headers["Authorization"] == f"Bearer {token}"
        cfg = await asyncio.to_thread(sim.api, "GET", "/api/config")
        assert b"surface" in cfg[1]
        assert token.encode() not in cfg[1], "the token leaks into the config"
        srv.tokens.clear()  # the host forgot the bridge: it is turned away and pairs again only after a new URL
        await again.ws.close()
        await asyncio.sleep(3)
        assert srv.refused >= 1
    finally:
        await srv.stop()


async def test_pairing_over_tls_pins_the_certificate(start):
    srv = await Server(PORT, tls=True).start()
    try:
        sim = start("mk2")
        await asyncio.to_thread(dismiss_prompt, sim)
        url = f"wss://127.0.0.1:{PORT}/surface"
        await asyncio.to_thread(sim.config, servers=[url], controller="server", setup_done=True)
        bridge = await srv.wait_bridge(20)
        assert "pair_code" not in bridge.hello
        cfg = json.loads((await asyncio.to_thread(sim.api, "GET", "/api/config"))[1])
        assert cfg["server_pins"][0] == srv.fingerprint
        await srv.stop()
        other = await Server(PORT, tls=True, tokens=srv.tokens).start()  # same port and token, another certificate
        srv = other
        assert await asyncio.to_thread(sim.wait, lambda: "certificate" in sim.status()["sources"][0]["error"], 20), (
            sim.status()["sources"]
        )
        assert srv.connected.empty()
    finally:
        await srv.stop()


@pytest.mark.parametrize("model", [*DISPLAY, "pedal"])
async def test_hello_describes_each_model(start, model):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, model)
        hello = bridge.hello
        assert hello["proto"] == 2 and hello["cache"] == {"bytes": 8 * 1024 * 1024, "slots": 1024}
        assert isinstance(hello["cache_epoch"], int)
        deck = (await asyncio.to_thread(sim.status))["decks"][0]
        d = bridge.decks[0]
        assert (d["name"], d["serial"], d["keys"], d["encoders"]) == (
            deck["model"], deck["serial"], deck["keys"], deck["encoders"],
        )  # fmt: skip
        keys = [t for t in d["targets"] if t["kind"] == "key"]
        assert [t["id"] for t in keys] == list(range(len(keys)))
        assert [t["id"] for t in d["targets"]] == list(range(len(d["targets"])))
        if model == "pedal":
            assert d["displays"] == [] and d["targets"] == []
            return
        disp = d["displays"][0]
        assert disp["formats"] == (["bmp"] if model in ("mini", "module6") else ["jpeg"]), disp
        assert (0, 0) in bridge.credits and bridge.credits[0, 0].frames > 0
        assert bridge.states[0]["display_on"] is True
        _, desc = await asyncio.to_thread(sim.screen, 0)
        assert bool(d["touch"]) == bool(desc["touch"])
    finally:
        await srv.stop()


def fmt_of(bridge: Bridge, deck: int = 0) -> tuple[int, str]:
    f = bridge.decks[deck]["displays"][0]["formats"][0]
    return (p.BMP, "bmp") if f == "bmp" else (p.JPEG, "jpeg")


@pytest.mark.parametrize("model", DISPLAY)
async def test_put_and_frame_draw_every_key_at_once(start, model):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, model)
        keys = [t for t in bridge.decks[0]["targets"] if t["kind"] == "key"]
        code, ext = fmt_of(bridge)
        for slot, t in enumerate(keys):
            await bridge.put(slot, solid(t["w"], t["h"], (220, 20, 20), ext), t["w"], t["h"], code)
        before = await asyncio.to_thread(pictures, sim, 0)
        shown = await bridge.show(0, [(t["id"], slot) for slot, t in enumerate(keys)])
        assert shown.us > 0 and not bridge.errors, (shown, bridge.errors)
        want = 1 if model in ("plus", "plus-xl") else len(keys)  # the + draws its keys into one LCD image
        new = lambda: drawn_since(sim, 0, before)  # noqa: E731
        assert await asyncio.to_thread(
            sim.wait, lambda: len(new()) >= want and all(near(Image.open(f), (220, 20, 20)) for f in new())
        ), sorted(f.name for f in new())
        await asyncio.to_thread(shot, sim, 0, f"{model}-surface")
        # the same page again costs one FRAME and no image
        again = await bridge.show(0, [(keys[0]["id"], 0)])
        assert again.us > 0
    finally:
        await srv.stop()


async def test_a_wrong_slot_size_or_format_is_refused_with_one_shown(start):
    srv = await Server(PORT).start()
    try:
        _, bridge = await paired_bridge(start, srv, "mk2")
        key = bridge.target(0)
        await bridge.put(1, solid(10, 10, (0, 0, 255)), 10, 10)
        await bridge.put(2, solid(key["w"], key["h"], (0, 0, 255), "bmp"), key["w"], key["h"], p.BMP)
        for slot, code in ((1, "bad_size"), (2, "bad_format"), (3, "empty_slot")):
            shown = await bridge.show(0, [(key["id"], slot)])
            assert shown.us == 0 and bridge.errors[-1]["code"] == code, bridge.errors
        assert (await bridge.show(0, [(200, 1)])).us == 0 and bridge.errors[-1]["code"] == "bad_target"
        assert (await bridge.show(3, [(key["id"], 1)])).us == 0 and bridge.errors[-1]["code"] == "no_deck"
        await bridge.send(p.msg(0x55, 0))
        await asyncio.sleep(0.3)
        assert bridge.errors[-1]["code"] == "unknown_type"
    finally:
        await srv.stop()


async def test_evict_and_a_full_cache(start):
    srv = await Server(PORT).start()
    try:
        _, bridge = await paired_bridge(start, srv, "mk2")
        key = bridge.target(0)
        red = solid(key["w"], key["h"], (220, 20, 20))
        junk = bytes(1_000_000)
        for slot in range(8):
            await bridge.put(slot, junk, 1, 1)
        await bridge.put(100, red, key["w"], key["h"])
        await bridge.put(8, junk, 1, 1)
        await asyncio.sleep(0.5)
        assert [e["code"] for e in bridge.errors] == ["cache_full"], bridge.errors
        assert bridge.credits[0, 0].bytes < 400_000, bridge.credits  # the host keeps its PUTs within it
        await bridge.send(p.evict([0, 1]))
        await asyncio.sleep(0.3)
        assert bridge.credits[0, 0].bytes > 2_000_000, bridge.credits
        await bridge.put(9, junk, 1, 1)  # fits only once the holes are closed up, which moves slot 100
        await asyncio.sleep(0.5)
        assert len(bridge.errors) == 1, bridge.errors
        assert (await bridge.show(0, [(key["id"], 100)])).us > 0, bridge.errors
        await bridge.send(p.evict([100]))
        assert (await bridge.show(0, [(key["id"], 100)])).us == 0 and bridge.errors[-1]["code"] == "empty_slot"
        await bridge.put(100, red, key["w"], key["h"])
        await bridge.send(p.evict())
        assert (await bridge.show(0, [(key["id"], 100)])).us == 0 and bridge.errors[-1]["code"] == "empty_slot"
    finally:
        await srv.stop()


async def test_a_second_host_takes_over_with_an_empty_cache(start):
    a = await Server(PORT, "a").start()
    b = await Server(PORT + 1, "b").start()
    try:
        sim, first = await paired_bridge(start, a, "mk2")
        await asyncio.to_thread(
            sim.config, servers=[f"ws://127.0.0.1:{PORT}/surface", f"ws://127.0.0.1:{PORT + 1}/surface"]
        )
        second = await b.wait_bridge(20)
        epoch = second.hello["cache_epoch"]
        key = first.target(0)
        await first.put(1, solid(key["w"], key["h"], (220, 20, 20)), key["w"], key["h"])
        assert (await first.show(0, [(key["id"], 1)])).us > 0
        assert (await second.show(0, [(key["id"], 1)])).us == 0 and second.errors[-1]["code"] == "standby"
        await second.role(True)
        assert await asyncio.to_thread(sim.wait, lambda: sim.status()["decks"][0]["writer"] == "server2", 5)
        assert (await second.show(0, [(key["id"], 1)])).us == 0 and second.errors[-1]["code"] == "empty_slot"
        assert (await first.show(0, [(key["id"], 1)])).us == 0 and first.errors[-1]["code"] == "standby"
        await first.role(True)  # back to the first: its old slots are gone as well
        assert await asyncio.to_thread(sim.wait, lambda: sim.status()["decks"][0]["writer"] == "server1", 5)
        assert (await first.show(0, [(key["id"], 1)])).us == 0 and first.errors[-1]["code"] == "empty_slot"
        await second.ws.close()
        again = await b.wait_bridge(20)
        assert again.hello["cache_epoch"] == (epoch + 2) % 2**32
    finally:
        await a.stop()
        await b.stop()


async def test_the_fallback_shows_when_the_host_drops_and_the_cache_outlives_a_restart(start):
    srv = await Server(PORT, ping=False).start()
    tokens = srv.tokens
    try:
        sim, bridge = await paired_bridge(start, srv, "plus")
        epoch = bridge.hello["cache_epoch"]
        keys = [t for t in bridge.decks[0]["targets"] if t["kind"] == "key"]
        blue, grey = solid(120, 120, (20, 20, 220)), solid(120, 120, (128, 128, 128))
        await bridge.put(1, blue, 120, 120)
        await bridge.put(2, grey, 120, 120)
        assert (await bridge.show(0, [(t["id"], 1) for t in keys])).us > 0
        await bridge.send(p.fallback(0, [(t["id"], 2) for t in keys]))
        await asyncio.sleep(0.2)
        await srv.stop()  # the host goes away
        img = await asyncio.to_thread(
            sim.wait, lambda: (i := sim.screen(0)[0]) and near(i.crop((0, 0, 120, 120)), (128, 128, 128)) and i, 10
        )
        assert img, "no fallback picture"
        assert (await asyncio.to_thread(sim.status))["decks"][0]["writer"] == "server1"
        await asyncio.to_thread(shot, sim, 0, "plus-fallback")
        srv = await Server(PORT, ping=False, tokens=tokens).start()  # back, and it knows the bridge
        bridge = await srv.wait_bridge(40)
        assert bridge.hello["cache_epoch"] == epoch, "the cache was dropped although no other host drew"
        await bridge.role(True)
        assert (await bridge.show(0, [(t["id"], 1) for t in keys])).us > 0, bridge.errors
        assert await asyncio.to_thread(
            sim.wait, lambda: near(sim.screen(0)[0].crop((0, 0, 120, 120)), (20, 20, 220)), 5
        )
    finally:
        await srv.stop()


async def test_without_a_fallback_the_deck_says_the_host_is_gone(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "mk2")
        await bridge.ws.close()
        await srv.stop()
        assert await asyncio.to_thread(sim.wait, lambda: sim.status()["decks"][0]["writer"] == "ui", 5)
    finally:
        await srv.stop()


async def test_credit_bounds_the_frames_in_flight_and_each_gets_one_shown(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "xl")
        credit = bridge.credits[0, 0]
        assert (credit.frames, credit.bytes) == (4, 8 * 1024 * 1024), credit
        key = bridge.target(0)
        await bridge.put(1, solid(key["w"], key["h"], (220, 20, 20)), key["w"], key["h"])
        assert (await asyncio.to_thread(sim.api, "POST", f"/api/deck/stall?deck={sim.serial(0)}"))[0] == 200
        # one key each: a newer image for the same key would replace a waiting one and end its frame early
        futs = [await bridge.frame(0, [(k, 1)]) for k in range(credit.frames + 2)]
        await asyncio.sleep(0.5)
        assert [e["code"] for e in bridge.errors] == ["busy", "busy"], bridge.errors
        assert [f.done() for f in futs] == [False] * credit.frames + [True, True]
        await asyncio.to_thread(sim.act, "0 unplug")  # the hung frames end, each with its SHOWN
        done = await asyncio.wait_for(asyncio.gather(*futs), 10)
        assert len(done) == len(futs)
        await asyncio.to_thread(sim.act, "0 plug")
    finally:
        await srv.stop()


async def test_a_stream_replaces_an_older_one_and_is_shown(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "plus")
        full = bridge.target(0, "full")
        futs = [await bridge.stream(0, full["id"], solid(800, 480, (0, 200 - i * 40, 0))) for i in range(4)]
        shown = await asyncio.wait_for(asyncio.gather(*futs), 10)
        assert shown[-1].us > 0 and shown[-1].stream
        assert len({s.id for s in shown}) == 4
        assert await asyncio.to_thread(sim.wait, lambda: near(sim.screen(0)[0], (0, 80, 0)), 5)
        strip = bridge.target(0, "strip")
        assert (await asyncio.wait_for(await bridge.stream(0, strip["id"], b"", p.BMP), 5)).us == 0
        assert bridge.errors[-1]["code"] == "bad_format"
    finally:
        await srv.stop()


async def test_brightness_and_state(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "mk2", "neo")
        await bridge.send(p.brightness(30))
        await bridge.send(p.brightness(55, 1))
        for deck, pct in ((0, 30), (1, 55)):
            assert await asyncio.to_thread(
                sim.wait, lambda d=deck, b=pct: (sim.decks / str(d) / "brightness").read_text().strip() == str(b), 5
            )
        for _ in range(20):
            if bridge.states.get(1, {}).get("brightness") == 55:
                break
            await asyncio.sleep(0.2)
        assert bridge.states[0] == {"display_on": True, "brightness": 30}, bridge.states
        assert bridge.states[1] == {"display_on": True, "brightness": 55}, bridge.states
    finally:
        await srv.stop()


async def test_hotplug_tells_the_host(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "mk2", "plus")
        serial = bridge.decks[1]["serial"]
        await asyncio.to_thread(sim.act, "1 unplug")
        assert await asyncio.wait_for(bridge.removed.get(), 5) == 1
        await asyncio.to_thread(sim.act, "1 plug")
        for _ in range(50):
            if 1 in bridge.decks:
                break
            await asyncio.sleep(0.1)
        assert bridge.decks[1]["serial"] == serial and bridge.decks[1]["encoders"] == 4
    finally:
        await srv.stop()


async def inputs(bridge: Bridge, n: int) -> list[p.Input]:
    return [await asyncio.wait_for(bridge.inputs.get(), 5) for _ in range(n)]


async def test_input_from_keys_encoders_and_touch(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "plus", "neo")
        await bridge.send(p.time_ms(int(time.time() * 1000)))  # taken, but the input keeps the board's clock
        since = time.monotonic()
        await asyncio.to_thread(sim.tap, 0, 1, 0.2)
        got = await inputs(bridge, 2)
        assert [(i.deck, i.kind, i.index) for i in got] == [(0, p.InputKind.KEY_DOWN, 1), (0, p.InputKind.KEY_UP, 1)]
        assert got[1].seq == got[0].seq + 1
        held = got[1].usb_time_us - got[0].usb_time_us
        assert 150_000 < held < (time.monotonic() - since) * 1e6, got
        await asyncio.to_thread(sim.act, "0 enc 2 1", "0 enc 2 0", "0 turn 3 -2", "0 turn 3 1")
        got = await inputs(bridge, 4)
        assert [(i.kind, i.index, i.value) for i in got] == [
            (p.InputKind.ENCODER_DOWN, 2, 0),
            (p.InputKind.ENCODER_UP, 2, 0),
            (p.InputKind.ENCODER_TURN, 3, -2),
            (p.InputKind.ENCODER_TURN, 3, 1),
        ]
        await asyncio.to_thread(sim.act, "0 touch 1 100 50", "0 touch 2 300 40", "0 touch 3 100 50 600 60")
        got = await inputs(bridge, 3)
        assert [i.kind for i in got] == [p.InputKind.TOUCH_TAP, p.InputKind.TOUCH_LONG, p.InputKind.TOUCH_SWIPE]
        assert (got[0].x, got[0].y) == (100, 430) and (got[2].x2, got[2].y2) == (600, 440), got  # display coordinates
        await asyncio.to_thread(sim.tap, 1, 8)  # the Neo's touch key, after its eight image keys
        got = await inputs(bridge, 2)
        assert [(i.deck, i.kind, i.index) for i in got] == [(1, p.InputKind.KEY_DOWN, 8), (1, p.InputKind.KEY_UP, 8)]
    finally:
        await srv.stop()


async def test_a_standby_host_gets_no_input(start):
    a = await Server(PORT, "a").start()
    b = await Server(PORT + 1, "b").start()
    try:
        sim, first = await paired_bridge(start, a, "mk2")
        await asyncio.to_thread(
            sim.config, servers=[f"ws://127.0.0.1:{PORT}/surface", f"ws://127.0.0.1:{PORT + 1}/surface"]
        )
        second = await b.wait_bridge(20)
        await asyncio.to_thread(sim.tap, 0, 0)
        assert len(await inputs(first, 2)) == 2
        await asyncio.sleep(0.3)
        assert second.inputs.empty()
    finally:
        await a.stop()
        await b.stop()
