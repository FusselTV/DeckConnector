"""The deck unplugged and plugged in again while it runs: the picture comes back by itself."""

import asyncio
import json
import time

from conftest import api, jpeg, status, strip_color

FULL = (0, 0, 800, 480)


async def live(server):
    return list(server.bridges.values())[-1] if server.bridges else await server.wait_bridge(60)


async def wait_device(present: bool, secs: float) -> float:
    start = time.time()
    while time.time() - start < secs:
        if ((await asyncio.to_thread(status))["device"] is not None) == present:
            return time.time() - start
        await asyncio.sleep(0.02)
    raise AssertionError(f"device {'never came back' if present else 'never left'}")


async def test_unplug_and_plug_in_again(servers, bridges):
    active = await live(servers[0])
    await active.send_image(FULL, jpeg((20, 40, 230)))
    await asyncio.sleep(0.5)
    assert max(range(3), key=lambda i: strip_color()[i]) == 2
    before = json.loads((await asyncio.to_thread(api, "GET", "/api/screen"))[1])["images"]
    assert (await asyncio.to_thread(api, "POST", "/api/deck/reconnect"))[0] == 200
    await wait_device(False, 5)
    back = await wait_device(True, 20)
    assert back < 10, f"the deck took {back:.1f} s to come back"
    for _ in range(50):  # the server's kept picture is drawn again, without the server sending it
        if max(range(3), key=lambda i: strip_color()[i]) == 2:
            break
        await asyncio.sleep(0.2)
    seen = await asyncio.to_thread(strip_color)
    assert max(range(3), key=lambda i: seen[i]) == 2, f"camera saw {seen} after the deck came back"
    assert active.device, "the server was told about the deck again"
    # the same deck: the bridge drew its kept picture again by itself
    assert json.loads((await asyncio.to_thread(api, "GET", "/api/screen"))[1])["images"] == before
