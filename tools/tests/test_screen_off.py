"""screen_off_s: the decks dim or go dark after that long without input. A press on a dark deck only wakes it; on a
dimmed one it works."""

import asyncio
import contextlib
import json
import time
from collections.abc import Callable

import pytest
from conftest import api, jpeg, put_config, status, strip_color

from dbtools.protocol import InputKind

FULL = (0, 0, 800, 480)


def screen(on: bool) -> None:
    assert api("POST", "/api/screen", json.dumps({"on": on}).encode())[0] == 200


def press(key: int, **extra) -> None:
    assert api("POST", "/api/input", json.dumps({"key": key} | extra).encode())[0] == 200


def wait_dark(within: float) -> float:
    start = time.monotonic()
    while not status()["screen_off"]:
        assert time.monotonic() - start < within, "the decks did not go dark"
        time.sleep(0.2)
    return time.monotonic() - start


@pytest.fixture
def off_after_10s():
    put_config({"screen_off_s": 10})
    screen(True)  # the idle time starts now
    yield
    put_config({"screen_off_s": 0})
    screen(True)


@pytest.mark.sim
def test_screen_off_s_is_checked():
    for bad in (-1, 1, 9, 86401, "60"):
        code, reply = api("PUT", "/api/config", json.dumps({"screen_off_s": bad}).encode())
        assert code == 400 and "screen_off_s" in json.loads(reply)["error"], bad
    for bad in (-10, 5, 15, 100, 30.5, "30"):
        code, reply = api("PUT", "/api/config", json.dumps({"screen_dim": bad}).encode())
        assert code == 400 and "screen_dim" in json.loads(reply)["error"], bad
    assert api("POST", "/api/screen", b'{"on": 1}')[0] == 400


def input_since() -> Callable[[], list]:
    log = json.loads(api("GET", "/api/input/log")[1])
    last = log[-1]["seq"] if log else 0
    return lambda: [(e["kind"], e["index"]) for e in json.loads(api("GET", f"/api/input/log?since={last}")[1])]


@pytest.mark.sim
def test_dimmed_after_the_idle_time_and_a_press_works():
    put_config({"screen_off_s": 10, "screen_dim": 30, "brightness": 80})
    screen(True)
    try:
        start = time.monotonic()
        while status()["decks"][0]["brightness"] != 30:
            assert time.monotonic() - start < 13, "the decks did not dim"
            time.sleep(0.2)
        assert not status()["screen_off"], "dimmed is not off: the screen switch stays on"
        seen = input_since()
        press(2, real="192.0.2.1")
        assert seen() == [("key_down", 2), ("key_up", 2)], "the press on a dimmed deck was swallowed"
        assert status()["decks"][0]["brightness"] == 80
    finally:
        put_config({"screen_off_s": 0, "screen_dim": 0})
        screen(True)


@pytest.mark.sim
def test_one_press_turns_the_switched_off_screen_on():
    put_config({"brightness": 70})
    screen(False)
    try:
        st = status()
        assert st["screen_off"] and st["decks"][0]["brightness"] == 0
        seen = input_since()
        press(1, real="192.0.2.1")
        st = status()
        assert not st["screen_off"] and st["decks"][0]["brightness"] == 70
        assert seen() == [], "the press that turned the screen on did something"
    finally:
        screen(True)


@pytest.mark.sim
def test_one_press_lights_a_deck_at_brightness_0():
    if status()["decks"][0]["writer"] != "ui":
        pytest.skip("a host draws: its press is not swallowed (tools/simdeck/test_wake.py covers that)")
    before = status()["brightness"]
    put_config({"brightness": 0})
    try:
        assert status()["decks"][0]["brightness"] == 0
        time.sleep(0.3)  # db_deck takes the new wake level on the next UI tick (200 ms)
        seen = input_since()
        press(1, real="192.0.2.1")
        assert status()["decks"][0]["brightness"] == 50, "a press left the deck black"
        assert seen() == [], "a press on the black deck did something"
        press(1, real="192.0.2.1")
        assert seen() == [("key_down", 1), ("key_up", 1)]
    finally:
        put_config({"brightness": before})


@pytest.mark.sim
def test_dark_after_the_idle_time_and_the_wake_press_is_swallowed(off_after_10s):
    assert wait_dark(13) > 9
    st = status()
    assert all(d["brightness"] == 0 for d in st["decks"]), st["decks"]
    put_config({"brightness": 40})  # remembered while dark
    assert status()["decks"][0]["brightness"] == 0 and status()["brightness"] == 40
    log = json.loads(api("GET", "/api/input/log")[1])
    last = log[-1]["seq"] if log else 0
    press(2, real="192.0.2.1")
    st = status()
    assert not st["screen_off"] and st["decks"][0]["brightness"] == 40
    assert json.loads(api("GET", f"/api/input/log?since={last}")[1]) == []
    press(2, real="192.0.2.1")
    seen = json.loads(api("GET", f"/api/input/log?since={last}")[1])
    assert [(e["kind"], e["index"]) for e in seen] == [("key_down", 2), ("key_up", 2)]


@pytest.mark.sim
def test_an_open_menu_keeps_the_decks_lit(off_after_10s):
    assert api("POST", "/api/menu")[0] == 200
    try:
        time.sleep(11)
        assert not status()["screen_off"]
    finally:
        api("DELETE", "/api/menu")
    time.sleep(2)
    assert not status()["screen_off"], "dark as soon as the menu closed: the idle time did not start again"
    wait_dark(10)
    assert api("POST", "/api/menu")[0] == 200  # a question or the menu wakes them
    try:
        time.sleep(0.5)
        assert not status()["screen_off"]
    finally:
        api("DELETE", "/api/menu")


async def test_the_deck_goes_black_and_the_wake_press_stays_on_the_deck(bridges):
    active, _ = bridges
    white, red = (240, 240, 240), (230, 20, 20)
    assert (await active.send_image(FULL, jpeg(white))).result.name == "SHOWN"
    await active.send_json({"t": "brightness", "pct": 100})
    await asyncio.sleep(0.5)
    bright = sum(await asyncio.to_thread(strip_color))
    await asyncio.to_thread(put_config, {"screen_off_s": 10})
    try:
        await asyncio.to_thread(screen, True)
        await asyncio.to_thread(wait_dark, 13)
        dark = sum(await asyncio.to_thread(strip_color))
        assert dark < bright / 3, f"camera saw {dark} dark against {bright} lit"
        await active.send_json({"t": "brightness", "pct": 60})
        assert (await active.send_image(FULL, jpeg(red))).result.name == "SHOWN"  # drawn while dark
        await asyncio.sleep(0.5)
        assert sum(await asyncio.to_thread(strip_color)) < bright / 3, "a brightness while dark lit the deck"
        while not active.inputs.empty():
            active.inputs.get_nowait()

        await asyncio.to_thread(press, 3)
        with contextlib.suppress(TimeoutError):
            got = await asyncio.wait_for(active.inputs.get(), 1.5)
            pytest.fail(f"the wake press reached the server: {got}")
        st = await asyncio.to_thread(status)
        assert not st["screen_off"] and st["decks"][0]["brightness"] == 60
        seen = await asyncio.to_thread(strip_color)
        assert max(range(3), key=lambda i: seen[i]) == 0 and sum(seen) > dark * 2, f"camera saw {seen} after waking"

        await asyncio.to_thread(press, 3)
        down = await asyncio.wait_for(active.inputs.get(), 3)
        up = await asyncio.wait_for(active.inputs.get(), 3)
        assert (down.kind, down.index, up.kind, up.index) == (InputKind.KEY_DOWN, 3, InputKind.KEY_UP, 3)
    finally:
        await asyncio.to_thread(put_config, {"screen_off_s": 0})
        await asyncio.to_thread(screen, True)
