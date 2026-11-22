"""Input simulated over the web API, bound to the test server that draws, reaches it like a real press."""

import asyncio
import contextlib
import json

import pytest
from conftest import api, status

from dbtools.protocol import InputKind
from dbtools.safety import real_input_target


async def post(body: dict, bound: bool = True) -> tuple[int, dict]:
    """`bound`: to the host that draws, or the press stays on the bridge."""
    if bound:
        body = body | {"real": real_input_target(await asyncio.to_thread(status))}
    code, reply = await asyncio.to_thread(api, "POST", "/api/input", json.dumps(body).encode())
    return code, json.loads(reply)


async def next_input(bridge):
    return await asyncio.wait_for(bridge.inputs.get(), 3)


async def test_key_encoder_and_touch_reach_the_server(bridges):
    active, standby = bridges
    while not active.inputs.empty():
        active.inputs.get_nowait()
    assert (await post({"key": 3}))[0] == 200
    down, up = await next_input(active), await next_input(active)
    assert (down.kind, down.index, up.kind, up.index) == (InputKind.KEY_DOWN, 3, InputKind.KEY_UP, 3)

    try:
        assert (await post({"key": 1, "action": "down"}))[0] == 200
        assert (await post({"key": 6, "action": "down"}))[0] == 200
        assert (await post({"key": 1, "action": "up"}))[0] == 200
        got = [await next_input(active) for _ in range(3)]
        assert [(e.kind, e.index) for e in got] == [
            (InputKind.KEY_DOWN, 1),
            (InputKind.KEY_DOWN, 6),
            (InputKind.KEY_UP, 1),
        ], "key 6 stays held while key 1 goes up"
    finally:  # a held key stays held until a reboot and breaks later tests
        await post({"key": 1, "action": "up"})
        await post({"key": 6, "action": "up"})
    with contextlib.suppress(TimeoutError):  # the releases above, until none comes
        while True:
            await asyncio.wait_for(active.inputs.get(), 0.5)

    assert (await post({"encoder": 2, "action": "turn", "ticks": -3}))[0] == 200
    turn = await next_input(active)
    assert (turn.kind, turn.index, turn.value) == (InputKind.ENCODER_TURN, 2, -3)

    assert (await post({"touch": "swipe", "x": 700, "y": 420, "x2": 100, "y2": 430}))[0] == 200
    swipe = await next_input(active)
    assert (swipe.kind, swipe.x, swipe.y, swipe.x2, swipe.y2) == (InputKind.TOUCH_SWIPE, 700, 420, 100, 430)
    assert swipe.deck == 0 and standby.inputs.empty(), "only the host that draws gets input"


@pytest.mark.parametrize(
    "body, error",
    [
        ({"key": 8}, "no such key"),
        ({"encoder": 0, "action": "turn", "ticks": 0}, "ticks"),
        ({"touch": "tap", "x": 10, "y": 10}, "touch strip"),
        ({"key": 0, "action": "turn"}, "action"),
        ({}, "key, encoder or touch"),
    ],
)
async def test_bad_input_is_refused(servers, body, error):
    code, reply = await post(body, bound=False)
    assert code == 400 and error in reply["error"]
