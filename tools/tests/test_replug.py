"""The deck unplugged and plugged in again while it runs: the picture comes back by itself."""

import asyncio
import json
import time

from conftest import api, jpeg, newest, status, strip_color

FULL = (0, 0, 800, 480)


async def wait_device(present: bool, secs: float) -> float:
    start = time.time()
    while time.time() - start < secs:
        if ((await asyncio.to_thread(status))["device"] is not None) == present:
            return time.time() - start
        await asyncio.sleep(0.02)
    raise AssertionError(f"device {'never came back' if present else 'never left'}")


async def test_unplug_and_plug_in_again(servers, bridges):
    active, _ = bridges
    ack = await active.send_image(FULL, jpeg((220, 20, 20)))
    assert ack.result.name == "SHOWN"
    await asyncio.sleep(0.5)
    assert max(range(3), key=lambda i: strip_color()[i]) == 0
    before = json.loads((await asyncio.to_thread(api, "GET", "/api/screen"))[1])["images"]
    assert (await asyncio.to_thread(api, "POST", "/api/deck/reconnect"))[0] == 200
    await wait_device(False, 5)
    back = await wait_device(True, 20)
    assert back < 10, f"the deck took {back:.1f} s to come back"
    for _ in range(50):  # the server's kept picture is drawn again, without the server sending it
        if max(range(3), key=lambda i: strip_color()[i]) == 0:
            break
        await asyncio.sleep(0.2)
    seen = await asyncio.to_thread(strip_color)
    assert max(range(3), key=lambda i: seen[i]) == 0, f"camera saw {seen} after the deck came back"
    assert active.device, "the server was told about the deck again"
    # the same deck keeps its picture: drawn again by the bridge, or sent again by the server
    after = json.loads((await asyncio.to_thread(api, "GET", "/api/screen"))[1])["images"]
    assert [(i["x"], i["y"], i["w"], i["h"]) for i in after] == [(i["x"], i["y"], i["w"], i["h"]) for i in before]


async def test_every_usb_address_keeps_the_deck_working(servers, bridges):
    """17 replugs pass at least one USB address that is a multiple of 16; the deck must draw after each."""
    active, _ = bridges
    errors = (await asyncio.to_thread(status))["counters"]["usb_errors"]
    for n in range(17):
        assert (await asyncio.to_thread(api, "POST", "/api/deck/reconnect"))[0] == 200
        await wait_device(False, 5)
        await wait_device(True, 20)
        if active.ws.state.name != "OPEN":
            active = await newest(servers[0])
        for i in range(4):
            try:
                ack = await active.send_image((13 + 219 * i, 12, 120, 120), jpeg((n * 15, 200, 60), (120, 120)))
            except TimeoutError:
                raise AssertionError(f"replug {n + 1}: the deck took no picture") from None
            assert ack.result.name == "SHOWN", f"replug {n + 1}: {ack.result.name}"
    assert (await asyncio.to_thread(status))["counters"]["usb_errors"] == errors, "an unplug is not a USB error"
