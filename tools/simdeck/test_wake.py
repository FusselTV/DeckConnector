"""One press brings a black deck back, whatever made it black, on the firmware's own stack."""

import asyncio
import subprocess
import time

from conftest import dismiss_prompt
from test_hosts import CHECK, companion  # noqa: F401  (a fixture)
from test_surface import PORT, paired_bridge, solid

from dbtools import protocol as p
from dbtools.server import Server


def brightness(sim, n: int = 0) -> str:
    return (sim.decks / f"{n}/brightness").read_text()


def satellite(sim, companion) -> str:  # noqa: F811
    sim.config(
        satellite=f"127.0.0.1:{companion.server_address[1]}", controller="companion", host="satellite", setup_done=True
    )
    assert sim.wait(lambda: companion.devices and sim.status()["decks"][0]["writer"] == "satellite", 15), sim.log()
    return companion.devices[0]


def test_a_deck_its_host_set_to_0_lights_and_the_host_gets_the_press(start, companion):  # noqa: F811
    sim = start("plus")
    dismiss_prompt(sim)
    dev = satellite(sim, companion)
    companion.send(f'BRIGHTNESS DEVICEID="{dev}" VALUE=0')
    assert sim.wait(lambda: brightness(sim) == "0", 5), brightness(sim)
    sim.tap(0, 4)
    assert sim.wait(lambda: any(x.startswith("KEY-PRESS") for x in companion.lines), 3), (
        f"the host that went dark itself did not get the press: {companion.lines[-5:]}"
    )
    assert sim.wait(lambda: brightness(sim) == "80", 3), f"the deck stayed at {brightness(sim)}"


def test_a_host_at_0_with_the_setting_at_0_lights_to_50_and_saves_nothing(start, companion):  # noqa: F811
    sim = start("plus")
    dismiss_prompt(sim)
    dev = satellite(sim, companion)
    sim.config(brightness=0)
    time.sleep(0.5)  # the wake level follows the setting on the next UI tick (200 ms)
    companion.send(f'BRIGHTNESS DEVICEID="{dev}" VALUE=0')
    assert sim.wait(lambda: brightness(sim) == "0", 5), brightness(sim)
    sim.tap(0, 4)
    assert sim.wait(lambda: any(x.startswith("KEY-PRESS") for x in companion.lines), 3)
    assert sim.wait(lambda: brightness(sim) == "50", 3), f"the deck stayed at {brightness(sim)}"
    time.sleep(0.5)
    assert sim.status()["brightness"] == 0, "the host's 0 changed the box's own setting"


def test_the_screen_switched_off_and_a_host_at_0_one_press_lifts_both_and_is_swallowed(start, companion):  # noqa: F811
    sim = start("plus")
    dismiss_prompt(sim)
    dev = satellite(sim, companion)
    companion.send(f'BRIGHTNESS DEVICEID="{dev}" VALUE=0')
    assert sim.wait(lambda: brightness(sim) == "0", 5), brightness(sim)
    assert sim.api("POST", "/api/screen", {"on": False})[0] == 200
    sim.tap(0, 4)
    assert sim.wait(lambda: brightness(sim) == "80", 3), f"the deck stayed at {brightness(sim)}"
    time.sleep(0.5)
    assert not any(x.startswith("KEY-PRESS") for x in companion.lines), "the press woke the switched-off deck and acted"
    assert not sim.status()["screen_off"]


async def test_a_live_server_owns_the_screen(start):
    srv = await Server(PORT).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "plus")
        await bridge.send(p.brightness(0))
        assert await asyncio.to_thread(sim.wait, lambda: brightness(sim) == "0", 5), brightness(sim)
        await asyncio.to_thread(sim.tap, 0, 4)
        got = [await asyncio.wait_for(bridge.inputs.get(), 3) for _ in range(2)]
        assert [(i.kind, i.index) for i in got] == [(p.InputKind.KEY_DOWN, 4), (p.InputKind.KEY_UP, 4)]
        await asyncio.sleep(0.5)
        assert brightness(sim) == "0", "the box lit a deck its server turned off"

        await bridge.send(p.brightness(60))
        assert await asyncio.to_thread(sim.wait, lambda: brightness(sim) == "60", 5), brightness(sim)
        assert (await asyncio.to_thread(sim.api, "POST", "/api/screen", {"on": False}))[0] == 200
        await asyncio.to_thread(sim.config, brightness=20)
        await asyncio.sleep(0.5)
        assert brightness(sim) == "60", "the screen switch or the box brightness reached the server's deck"
        await asyncio.to_thread(sim.tap, 0, 2)
        got = [await asyncio.wait_for(bridge.inputs.get(), 3) for _ in range(2)]
        assert [(i.kind, i.index) for i in got] == [(p.InputKind.KEY_DOWN, 2), (p.InputKind.KEY_UP, 2)]
    finally:
        await srv.stop()


async def test_once_the_server_is_gone_a_press_lights_its_black_deck(start):
    srv = await Server(PORT, ping=False).start()
    try:
        sim, bridge = await paired_bridge(start, srv, "plus")
        keys = [t["id"] for t in bridge.decks[0]["targets"] if t["kind"] == "key"]
        await bridge.put(1, solid(120, 120, (128, 128, 128)), 120, 120)
        await bridge.send(p.fallback(0, [(k, 1) for k in keys]))
        await bridge.send(p.brightness(0))
        assert await asyncio.to_thread(sim.wait, lambda: brightness(sim) == "0", 5), brightness(sim)
        await srv.stop()
        assert await asyncio.to_thread(sim.wait, lambda: not sim.status()["sources"][0]["connected"], 10)
        assert sim.status()["decks"][0]["writer"] == "server1", "no fallback picture"
        await asyncio.to_thread(sim.tap, 0, 4)
        assert await asyncio.to_thread(sim.wait, lambda: brightness(sim) == "80", 3), f"it stayed at {brightness(sim)}"
    finally:
        await srv.stop()


def test_the_pin_pad_is_never_black(start, companion):  # noqa: F811
    sim = start("plus")
    dismiss_prompt(sim)
    dev = satellite(sim, companion)
    sim.config(brightness=0)
    companion.send(f'LOCKED-STATE DEVICEID="{dev}" LOCKED=1 CHARACTER_COUNT=0 ROTATION=0')
    assert sim.wait(lambda: sim.status()["decks"][0]["locked"], 5)
    assert sim.wait(lambda: brightness(sim) == "20", 3), f"the PIN pad shows at {brightness(sim)}"


def test_brightness_0_on_the_box_one_press_lights_it_and_it_stays(start):
    sim = start("plus")
    dismiss_prompt(sim)
    sim.config(setup_done=True, brightness=0)
    assert sim.wait(lambda: brightness(sim) == "0", 5), brightness(sim)
    keys = sim.screen(0)[1]["keys"]
    menu = sum(k["y"] == keys[0]["y"] for k in keys) - 1  # the home screen's menu key
    sim.tap(0, menu)
    assert sim.wait(lambda: brightness(sim) == "50", 3), f"one press left it at {brightness(sim)}"
    assert sim.wait(lambda: sim.status()["brightness"] == 50, 3), "the setting still says 0"
    time.sleep(1.5)  # a redraw of the home screen must not take it back to 0
    assert brightness(sim) == "50"
    assert not sim.status()["menu_open"], "the waking press opened the menu"


def test_a_dimmed_deck_stays_dimmed_when_a_dock_host_sets_its_brightness(start):
    sim = start("plus")
    sim.tolerated = ("GET_FEATURE 0xa1", "GET_FEATURE 0x08", "GET_FEATURE 0x04", "SET_FEATURE 03 06 of 6 bytes")
    dismiss_prompt(sim)
    sim.config(controller="companion", host="", setup_done=True, screen_off_s=10, screen_dim=30)
    assert sim.wait(lambda: brightness(sim) == "30", 15), f"not dimmed: {brightness(sim)}"
    run = subprocess.run(["node", CHECK, "127.0.0.1:5343"], cwd=CHECK.parent, capture_output=True, text=True,
                         timeout=40)  # fmt: skip
    assert run.returncode == 0, run.stdout
    assert brightness(sim) == "30", f"the dock host's 70 % came through: {brightness(sim)}"
    assert not sim.status()["screen_off"]


def test_a_brightness_set_from_the_web_while_the_menu_shows_stays(start):
    sim = start("plus")
    dismiss_prompt(sim)
    sim.config(setup_done=True)
    assert sim.api("POST", "/api/menu")[0] == 200
    assert sim.wait(lambda: sim.status()["menu_open"], 3)
    sim.config(brightness=40)
    time.sleep(1)  # a few UI ticks: the menu must not put the brightness from when it opened back
    assert brightness(sim) == "40", f"the open menu took the deck back to {brightness(sim)}"
    assert sim.api("DELETE", "/api/menu")[0] == 200
    assert sim.wait(lambda: not sim.status()["menu_open"], 3)
    time.sleep(0.5)
    assert brightness(sim) == "40", f"closing the menu took the deck back to {brightness(sim)}"
