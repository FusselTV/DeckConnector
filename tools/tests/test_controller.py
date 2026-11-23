"""Switching who controls the deck, and reading back what the deck shows."""

import asyncio
import json

from conftest import api, jpeg, paint, status


# HTTP runs in a thread: the test servers share this event loop and must keep answering the bridge.
async def http(method: str, path: str, body: bytes | None = None) -> tuple[int, bytes]:
    return await asyncio.to_thread(api, method, path, body)


async def screen() -> dict:
    return json.loads((await http("GET", "/api/screen"))[1])


async def set_controller(name: str) -> None:
    code, reply = await http("PUT", "/api/config", json.dumps({"controller": name}).encode())
    assert code == 200 and json.loads(reply)["restart"] is False


async def test_screen_shows_what_the_server_drew(bridges):
    active, _ = bridges
    full, key = active.target(0, "full"), active.target(0)
    full_img, key_img = jpeg((10, 120, 200)), jpeg((250, 200, 0), (key["w"], key["h"]))
    await active.put(1, full_img, full["w"], full["h"])
    await active.put(2, key_img, key["w"], key["h"])
    assert (await active.show(0, [(key["id"], 2)])).us > 0
    covered = (await screen())["images"][-1]["url"]
    assert (await active.show(0, [(full["id"], 1)])).us > 0
    assert (await active.show(0, [(key["id"], 2)])).us > 0
    sc = await screen()
    assert sc["writer"] == "server1" and (sc["width"], sc["height"]) == (800, 480)
    rects = [(i["x"], i["y"], i["w"], i["h"]) for i in sc["images"]]
    rect = lambda t: (t["x"], t["y"], t["w"], t["h"])  # noqa: E731
    assert rects == [rect(full), rect(key)], "the full image covers all older ones, the key lies on top"
    assert (await http("GET", sc["images"][0]["url"]))[1] == full_img
    assert (await http("GET", sc["images"][1]["url"]))[1] == key_img
    assert (await http("GET", covered))[0] == 404


async def test_switching_keeps_servers_connected(bridges):
    active, standby = bridges
    assert (await paint(active, (200, 30, 120))).us > 0
    before = (await screen())["images"]
    try:
        await set_controller("companion")
        assert (await asyncio.to_thread(status))["controller"] == "companion"
        writer = (await screen())["writer"]
        assert writer in ("ui", "satellite") or writer.startswith("dock"), writer
        for bridge in (active, standby):
            assert (await paint(bridge, "red")).us == 0 and bridge.errors[-1]["code"] == "standby"
        assert not active.closed.is_set() and not standby.closed.is_set()
    finally:
        await set_controller("server")
    await wait_writer("server1")  # the host that asked last draws again without asking anew
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


async def test_the_host_that_asks_last_draws(servers, bridges):
    first, second = bridges
    try:
        await second.role(True)
        await wait_writer("server2")
        drawing = [x for x in (await asyncio.to_thread(status))["sources"] if x["drawing"]]
        assert [x["host"] for x in drawing] == [servers[1].url]
        assert (await paint(first, "red")).us == 0 and first.errors[-1]["code"] == "standby"
        assert (await paint(second, "green")).us > 0
    finally:
        await first.role(True)
    await wait_writer("server1")
    assert (await paint(second, "red")).us == 0 and second.errors[-1]["code"] == "standby"


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
