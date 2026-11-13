"""Switching who controls the deck, and reading back what the deck shows."""

import asyncio
import json

from conftest import api, jpeg, status

from dbtools.protocol import AckResult

FULL = (0, 0, 800, 480)
KEY0 = (13, 12, 120, 120)


# HTTP runs in a thread: the test servers share this event loop and must keep answering the bridge.
async def http(method: str, path: str, body: bytes | None = None) -> tuple[int, bytes]:
    return await asyncio.to_thread(api, method, path, body)


async def screen() -> dict:
    return json.loads((await http("GET", "/api/screen"))[1])


async def set_controller(name: str) -> None:
    code, reply = await http("PUT", "/api/config", json.dumps({"controller": name}).encode())
    assert code == 200 and json.loads(reply)["restart"] is False


async def wait_role(bridge, active: bool) -> None:
    for _ in range(50):
        if bridge.active == active:
            return
        await asyncio.sleep(0.1)
    assert bridge.active == active


async def test_screen_shows_what_the_server_drew(bridges):
    active, _ = bridges
    full, key = jpeg((10, 120, 200)), jpeg((250, 200, 0), (120, 120))
    assert (await active.send_image(KEY0, key)).result == AckResult.SHOWN
    covered = (await screen())["images"][-1]["url"]
    assert (await active.send_image(FULL, full)).result == AckResult.SHOWN
    assert (await active.send_image(KEY0, key)).result == AckResult.SHOWN
    sc = await screen()
    assert sc["writer"] == "server1" and (sc["width"], sc["height"]) == (800, 480)
    rects = [(i["x"], i["y"], i["w"], i["h"]) for i in sc["images"]]
    assert rects == [FULL, KEY0], "the full image covers all older ones, the key lies on top"
    assert (await http("GET", sc["images"][0]["url"]))[1] == full
    assert (await http("GET", sc["images"][1]["url"]))[1] == key
    assert (await http("GET", covered))[0] == 404


async def test_switching_keeps_servers_connected(bridges):
    active, standby = bridges
    sent = jpeg((200, 30, 120))
    assert (await active.send_image(FULL, sent)).result == AckResult.SHOWN
    before = (await screen())["images"]
    try:
        await set_controller("companion")
        await wait_role(active, False)
        assert not standby.active
        assert (await asyncio.to_thread(status))["controller"] == "companion"
        writer = (await screen())["writer"]
        assert writer in ("ui", "satellite") or writer.startswith("dock"), writer
        assert (await active.send_image(FULL, jpeg("red"))).result == AckResult.REJECTED_ROLE
    finally:
        await set_controller("server")
    await wait_role(active, True)
    sc = await screen()
    assert sc["writer"] == "server1"
    assert sc["images"] == before, "the kept picture is back without the server sending it again"


async def wait_writer(prefix: str) -> None:
    for _ in range(50):
        if (await screen())["writer"].startswith(prefix):
            return
        await asyncio.sleep(0.1)
    assert (await screen())["writer"].startswith(prefix)


async def test_menu_opens_from_the_web_and_names_its_chord(bridges):
    sc = await screen()
    chord = sc["menu_chord"]
    count = sc["encoders"] if chord["on"] == "encoders" else len(sc["keys"])
    assert chord["first"] == 0 and chord["last"] == count - 1
    assert (await http("POST", "/api/menu"))[0] == 200
    try:
        await wait_writer("ui")
        top = min(r["y"] for r in sc["keys"])
        close = max((r["x"], i) for i, r in enumerate(sc["keys"]) if r["y"] == top)[1]  # top right closes the menu
        assert (await http("POST", "/api/input", json.dumps({"key": close}).encode()))[0] == 200
        await wait_writer("server")
    finally:
        await http("DELETE", "/api/menu")


async def put_config(body: dict) -> None:
    assert (await http("PUT", "/api/config", json.dumps(body).encode()))[0] == 200


async def test_picking_one_of_two_servers(servers, bridges):
    first, second = (list(s.bridges.values())[-1] for s in servers)
    try:
        await put_config({"controller": "server", "host": servers[1].url})
        await wait_role(second, True)
        await wait_role(first, False)
        await wait_writer("server2")
        chosen = [x for x in (await asyncio.to_thread(status))["sources"] if x["chosen"]]
        assert [x["host"] for x in chosen] == [servers[1].url]

        # while a host draws, the deck has no host keys (the home screen has them when nobody draws)
        await put_config({"host": servers[0].url})
        await wait_role(first, True)
        await wait_role(second, False)
        await wait_writer("server1")
        assert json.loads((await http("GET", "/api/config"))[1])["host"] == servers[0].url
    finally:
        await put_config({"controller": "server", "host": ""})
    await wait_role(first, True)


async def test_the_menu_chord_and_hold_time_are_settings(bridges):
    keys = len((await screen())["keys"])
    hold = lambda action: [  # noqa: E731
        http("POST", "/api/input", json.dumps({"key": k, "action": action}).encode()) for k in (0, keys - 1)
    ]
    try:
        await put_config({"menu_chord": "keys", "menu_hold_ms": 800})
        assert (await screen())["menu_chord"] == {"on": "keys", "first": 0, "last": keys - 1, "hold_ms": 800}
        for r in hold("down"):
            assert (await r)[0] == 200
        await asyncio.sleep(0.4)
        assert (await screen())["writer"].startswith("server"), "the menu opened before the hold time"
        await wait_writer("ui")
        for r in hold("up"):
            await r
        top = min(r["y"] for r in (await screen())["keys"])
        close = max((r["x"], i) for i, r in enumerate((await screen())["keys"]) if r["y"] == top)[1]
        assert (await http("POST", "/api/input", json.dumps({"key": close}).encode()))[0] == 200
        await wait_writer("server")

        await put_config({"menu_chord": "off"})
        assert (await screen())["menu_chord"] == {"on": "off"}
        for r in hold("down"):
            await r
        await asyncio.sleep(1.2)
        assert (await screen())["writer"].startswith("server"), "off still opened the menu"
        for r in hold("up"):
            await r
    finally:
        await http("DELETE", "/api/menu")
        await put_config({"menu_chord": "auto", "menu_hold_ms": 2000})
