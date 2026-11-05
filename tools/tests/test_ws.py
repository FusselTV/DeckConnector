"""The /api/ws socket: state on connect and on change, every press, commands with results."""

import asyncio
import base64
import json

import pytest
import websockets
from conftest import HOST, PASSWORD

AUTH = {"Authorization": "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode()}


async def next_of(ws, kind: str, timeout: float = 5) -> dict:
    """The next message of `kind`; others that arrive meanwhile are kept for later calls."""
    kept = getattr(ws, "kept", [])
    ws.kept = kept
    for i, msg in enumerate(kept):
        if msg["t"] == kind:
            return kept.pop(i)

    async def wait():
        while True:
            msg = json.loads(await ws.recv())
            if msg["t"] == kind:
                return msg
            kept.append(msg)

    return await asyncio.wait_for(wait(), timeout)


async def result_of(ws, id: int) -> dict:
    for _ in range(10):
        result = await next_of(ws, "result")
        if result["id"] == id:
            return result
    raise AssertionError(f"no result for {id}")


async def state_with(ws, controller: str) -> dict:
    for _ in range(10):
        state = await next_of(ws, "state")
        if state["controller"] == controller:
            return state
    raise AssertionError(f"no state with controller {controller}")


async def test_state_input_and_commands(servers):
    async with websockets.connect(f"ws://{HOST}/api/ws", additional_headers=AUTH) as ws:
        state = await next_of(ws, "state")
        assert state["controller"] == "server" and "device" in state and "uptime_s" not in state

        await ws.send(json.dumps({"t": "input", "id": 7, "key": 2}))
        down = await next_of(ws, "input")
        assert (down["kind"], down["index"]) == ("key_down", 2)
        assert (await result_of(ws, 7))["status"] == 200

        await ws.send(json.dumps({"t": "set", "id": 8, "controller": "app"}))
        assert (await result_of(ws, 8))["status"] == 200
        await state_with(ws, "app")
        await ws.send(json.dumps({"t": "set", "id": 9, "controller": "server"}))
        await state_with(ws, "server")

        await ws.send(json.dumps({"t": "set", "id": 10, "brightness": 101}))
        bad = await result_of(ws, 10)
        assert bad["status"] == 400 and "0 to 100" in bad["reply"]["error"]
        await ws.send("not json")
        assert (await result_of(ws, 0))["status"] == 400


async def test_needs_login_and_our_origin():
    with pytest.raises(websockets.InvalidStatus) as no_login:
        async with websockets.connect(f"ws://{HOST}/api/ws"):
            pass
    assert no_login.value.response.status_code == 401
    with pytest.raises(websockets.InvalidStatus) as foreign:
        async with websockets.connect(f"ws://{HOST}/api/ws", additional_headers=AUTH, origin="http://evil.example"):
            pass
    assert foreign.value.response.status_code == 403
