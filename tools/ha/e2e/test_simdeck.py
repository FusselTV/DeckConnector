"""A real Home Assistant in Docker against firmware/simdeck with a mix of deck models: the devices and event
entities each model gets, its device triggers, its camera, presses at the decks and an unplugged deck. KEEP=1
leaves both running."""

import asyncio
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import aiohttp
import pytest
from test_docker import IMAGE, flow, onboard, wait_for_ha

ROOT = Path(__file__).resolve().parents[3]
SIM = ROOT / "firmware/simdeck/build/deckbridge_simdeck.elf"
SIM_PORT = int(os.environ.get("DC_HA_SIMDECK_PORT", "18471"))
HA_PORT = int(os.environ.get("DC_HA_SIMDECK_HA_PORT", "18131"))
HA = f"http://127.0.0.1:{HA_PORT}"
NAME = f"dc-ha-simdeck-{HA_PORT}"
WORK = Path(os.environ.get("DC_HA_WORK", Path.home() / ".cache/ha-work/simdeck"))
MODELS = {  # vid:pid, name, maker, keys, dials, touch strip
    "plus": ("0fd9:0084", "Stream Deck +", "Elgato", 8, 4, True),
    "neo": ("0fd9:009a", "Stream Deck Neo", "Elgato", 10, 0, False),
    "pedal": ("0fd9:0086", "Stream Deck Pedal", "Elgato", 3, 0, False),
    "n4": ("6602:1001", "Stream Dock N4", "Mirabox", 14, 5, False),
}

pytestmark = pytest.mark.skipif(not SIM.exists(), reason="build firmware/simdeck first")


def act(*lines: str) -> None:
    fd = os.open(WORK / "decks/ctl", os.O_WRONLY | os.O_NONBLOCK)  # fails at once when the simulator is gone
    try:
        os.write(fd, "".join(f"{line}\n" for line in lines).encode())
    finally:
        os.close(fd)


@pytest.fixture(scope="module")
def sim():
    shutil.rmtree(WORK / "decks", ignore_errors=True)
    (WORK / "decks").mkdir(parents=True)
    env = {
        **os.environ,
        "DB_SIM_PORT": str(SIM_PORT),
        "DB_SIM_PASSWORD": "sim-pass",
        "DB_SIM_DECK_MODELS": ",".join(m[0] for m in MODELS.values()),
        "DB_SIM_DECK_DIR": str(WORK / "decks"),
    }
    with (WORK / "sim.log").open("wb") as log:
        proc = subprocess.Popen([SIM], env=env, stdout=log, stderr=subprocess.STDOUT)
    WORK.joinpath("events.json").unlink(missing_ok=True)
    yield proc
    assert proc.poll() is None, "the simulator ended"
    if not os.environ.get("KEEP"):
        proc.kill()
    log = (WORK / "sim.log").read_text(errors="replace").splitlines()
    bad = [x for x in log if x.startswith(("deck_emu ", "mirabox_emu ", "sim_usb "))]
    assert not bad, bad


@pytest.fixture(scope="module")
def ha(sim):
    config = WORK / "config"
    subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)
    if config.exists():
        subprocess.run(
            ["docker", "run", "--rm", "-v", f"{WORK}:/w", "--entrypoint", "rm", IMAGE, "-rf", "/w/config"], check=True
        )
    (config / "custom_components").mkdir(parents=True)
    shutil.copytree(ROOT / "custom_components/deckconnector", config / "custom_components/deckconnector")
    (config / "configuration.yaml").write_text("default_config:\n")
    subprocess.run(
        ["docker", "run", "-d", "--name", NAME, "-p", f"127.0.0.1:{HA_PORT}:8123",
         "--add-host", "host.docker.internal:host-gateway", "-v", f"{config}:/config", IMAGE],
        check=True, capture_output=True,
    )  # fmt: skip
    yield
    if not os.environ.get("KEEP"):
        subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)


class Ws:
    def __init__(self, ws):
        self.ws, self.id = ws, 0

    async def call(self, **msg):
        self.id += 1
        await self.ws.send_json({"id": self.id, **msg})
        while True:
            got = await self.ws.receive_json(timeout=10)
            if got.get("id") == self.id:
                assert got.get("success", True), got
                return got.get("result")


async def setup(s: aiohttp.ClientSession) -> dict:
    import test_docker

    test_docker.HA, test_docker.CLIENT = HA, f"{HA}/"
    await wait_for_ha(s)
    auth = {"Authorization": f"Bearer {await onboard(s)}"}
    async with s.put(
        f"http://127.0.0.1:{SIM_PORT}/api/config",
        json={"max_decks": 4, "setup_done": True},
        auth=aiohttp.BasicAuth("admin", "sim-pass"),
        headers={"X-DeckConnector": "1"},
    ) as r:
        assert r.status == 200, await r.text()
    result = await flow(s, auth, None, {"handler": "deckconnector"})
    result = await flow(s, auth, result["flow_id"], {"host": f"host.docker.internal:{SIM_PORT}"})
    result = await flow(s, auth, result["flow_id"], {"next_step_id": "password"})
    if result.get("type") == "form" and result.get("step_id") == "password":
        result = await flow(s, auth, result["flow_id"], {"password": "sim-pass"})
    assert result["type"] == "create_entry", result
    return auth


async def states(s, auth) -> dict:
    async with s.get(f"{HA}/api/states", headers=auth) as r:
        return {e["entity_id"]: e for e in await r.json()}


async def test_every_model_gets_its_device_events_triggers_and_camera(sim, ha):
    async with aiohttp.ClientSession() as s:
        auth = await setup(s)
        await asyncio.sleep(3)
        async with s.ws_connect(f"{HA}/api/websocket") as raw:
            await raw.receive_json()
            await raw.send_json({"type": "auth", "access_token": auth["Authorization"][7:]})
            await raw.receive_json()
            ws = Ws(raw)
            devices = [
                d
                for d in await ws.call(type="config/device_registry/list")
                if any(i[0] == "deckconnector" for i in d["identifiers"]) and d["serial_number"]
            ]
            entities = [
                e for e in await ws.call(type="config/entity_registry/list") if e["platform"] == "deckconnector"
            ]
            got = {}
            for dev in devices:
                mine = [e["entity_id"] for e in entities if e["device_id"] == dev["id"]]
                events = [e for e in mine if e.startswith("event.")]
                assert len(events) == 1, (dev["model"], events)
                state = (await states(s, auth))[events[0]]
                triggers = await ws.call(type="device_automation/trigger/list", device_id=dev["id"])
                picture = None
                for cam in (e for e in mine if e.startswith("camera.")):
                    async with s.get(f"{HA}/api/camera_proxy/{cam}", headers=auth) as r:
                        picture = r.status, r.content_type
                types = state["attributes"]["event_types"]
                got[dev["model"]] = (dev["manufacturer"], types, len(triggers), picture, events[0])
        print(got)
        for _, name, maker, keys, dials, touch in MODELS.values():
            manufacturer, types, n_triggers, picture, _ = got[name]
            assert manufacturer == maker, name
            want = 2 * keys + 4 * dials + (4 if touch else 0)
            assert len(types) == want, (name, types)
            assert n_triggers == keys + 3 * dials + (4 if touch else 0), name  # presses and turns, no releases
            assert picture == (None if name == "Stream Deck Pedal" else (200, "image/jpeg")), (name, picture)
        WORK.joinpath("events.json").write_text(json.dumps({name: got[name][4] for name in got}))


async def until(cond, secs: float = 15) -> bool:
    async with aiohttp.ClientSession() as s:
        import test_docker

        auth = await test_docker.login(s)
        for _ in range(int(secs * 2)):
            if cond(await states(s, auth)):
                return True
            await asyncio.sleep(0.5)
    return False


async def wait_event(s, auth, entity: str, want: str, secs: float = 8) -> dict:
    end = time.monotonic() + secs
    while time.monotonic() < end:
        st = (await states(s, auth)).get(entity, {})
        if st.get("attributes", {}).get("event_type") == want:
            return st
        await asyncio.sleep(0.3)
    raise AssertionError(f"{entity}: no {want}, last {st.get('attributes', {}).get('event_type')}")


async def test_presses_at_each_deck_and_an_unplugged_deck(sim, ha):
    entity = json.loads(WORK.joinpath("events.json").read_text())  # written by the test above
    async with aiohttp.ClientSession() as s:
        import test_docker

        auth = await test_docker.login(s)
        plus, neo, pedal, n4 = (entity[m[1]] for m in MODELS.values())
        act("0 key 3 1")
        await wait_event(s, auth, plus, "key_4_press")
        act("0 key 3 0", "0 turn 1 2")
        st = await wait_event(s, auth, plus, "dial_2_right")
        assert st["attributes"]["ticks"] == 2
        act("0 touch 3 100 50 600 50")
        await wait_event(s, auth, plus, "touch_swipe_right")
        act("1 key 9 1")  # the Neo's right touch point
        await wait_event(s, auth, neo, "key_10_press")
        act("1 key 9 0", "2 key 2 1")
        await wait_event(s, auth, pedal, "key_3_press")
        act("2 key 2 0", "3 rc 3 1 -1")  # N4: second knob turned left
        await wait_event(s, auth, n4, "dial_2_left")
        act("3 rc 2 4 +1")  # a swipe on the strip turns its fifth control
        await wait_event(s, auth, n4, "dial_5_right")
        act("3 unplug")
        assert await until(lambda st: st[n4]["state"] == "unavailable")
        act("3 plug")
        assert await until(lambda st: st[n4]["state"] != "unavailable")
        act("3 rc 0 0 1")
        await wait_event(s, auth, n4, "key_1_press")
