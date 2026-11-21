"""A real Home Assistant in Docker sets up the integration against firmware/sim through the config flow REST API,
pairs by a confirm on the (simulated) deck, and sees a press of key 1 as an event. KEEP=1 leaves both running."""

import asyncio
import os
from pathlib import Path
import shutil
import subprocess
import time

import aiohttp
import pytest

ROOT = Path(__file__).resolve().parents[3]
SIM = ROOT / "firmware/sim/build/deckbridge_sim.elf"
IMAGE = "ghcr.io/home-assistant/home-assistant:stable"
SIM_PORT = int(os.environ.get("DC_HA_SIM_PORT", "18470"))
HA_PORT = int(os.environ.get("DC_HA_PORT", "18123"))
HA = f"http://127.0.0.1:{HA_PORT}"
NAME = f"dc-ha-e2e-{HA_PORT}"
CLIENT = f"{HA}/"
WORK = Path(os.environ.get("DC_HA_WORK", Path.home() / ".cache/ha-work/e2e"))


@pytest.fixture(scope="module")
def press_file():
    WORK.mkdir(parents=True, exist_ok=True)
    press = WORK / "press"
    press.unlink(missing_ok=True)
    env = {
        **os.environ,
        "DB_SIM_PORT": str(SIM_PORT),
        "DB_SIM_DECKS": "2",
        "DB_SIM_PASSWORD": "sim-pass",
        "DB_SIM_LOGIN_ACCEPT": "1",
        "DB_SIM_PRESS_FILE": str(press),
    }
    sim = subprocess.Popen([SIM], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    yield press
    if not os.environ.get("KEEP"):
        sim.kill()


@pytest.fixture(scope="module")
def ha(press_file):
    config = WORK / "config"
    if config.exists():
        subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)
        subprocess.run(
            ["docker", "run", "--rm", "-v", f"{WORK}:/w", "--entrypoint", "rm", IMAGE, "-rf", "/w/config"],
            check=True,
        )
    (config / "custom_components").mkdir(parents=True)
    shutil.copytree(ROOT / "custom_components/deckconnector", config / "custom_components/deckconnector")
    (config / "configuration.yaml").write_text("default_config:\nautomation: !include automations.yaml\n")
    (config / "automations.yaml").write_text("[]\n")
    subprocess.run(
        [
            "docker", "run", "-d", "--name", NAME,
            "-p", f"127.0.0.1:{HA_PORT}:8123",
            "--add-host", "host.docker.internal:host-gateway",
            "-v", f"{config}:/config",
            IMAGE,
        ],
        check=True,
        capture_output=True,
    )  # fmt: skip
    yield
    if not os.environ.get("KEEP"):
        subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)


async def wait_for_ha(s: aiohttp.ClientSession) -> None:
    end = time.monotonic() + 180
    while time.monotonic() < end:
        try:
            async with s.get(f"{HA}/api/onboarding") as r:
                if r.status == 200:
                    return
        except aiohttp.ClientError:
            pass
        await asyncio.sleep(1)
    raise TimeoutError("Home Assistant did not start")


async def onboard(s: aiohttp.ClientSession) -> str:
    user = {"client_id": CLIENT, "name": "Test", "username": "test", "password": "test-pass-1", "language": "en"}
    async with s.post(f"{HA}/api/onboarding/users", json=user) as r:
        code = (await r.json())["auth_code"]
    form = {"grant_type": "authorization_code", "code": code, "client_id": CLIENT}
    async with s.post(f"{HA}/auth/token", data=form) as r:
        token = (await r.json())["access_token"]
    auth = {"Authorization": f"Bearer {token}"}
    for step, body in (
        ("core_config", {}),
        ("analytics", {}),
        ("integration", {"client_id": CLIENT, "redirect_uri": CLIENT}),
    ):
        async with s.post(f"{HA}/api/onboarding/{step}", json=body, headers=auth) as r:
            assert r.status == 200, (step, await r.text())
    return token


async def flow(s: aiohttp.ClientSession, auth: dict, flow_id: str | None, body: dict) -> dict:
    url = f"{HA}/api/config/config_entries/flow" + (f"/{flow_id}" if flow_id else "")
    async with s.post(url, json=body, headers=auth) as r:
        assert r.status == 200, await r.text()
        return await r.json()


async def test_pair_and_press(press_file, ha):
    async with aiohttp.ClientSession() as s:
        await wait_for_ha(s)
        auth = {"Authorization": f"Bearer {await onboard(s)}"}
        result = await flow(s, auth, None, {"handler": "deckconnector"})
        flow_id = result["flow_id"]
        result = await flow(s, auth, flow_id, {"host": f"host.docker.internal:{SIM_PORT}"})
        assert result["type"] == "menu", result
        result = await flow(s, auth, flow_id, {"next_step_id": "pair"})
        assert result["type"] == "progress", result
        for _ in range(30):
            await asyncio.sleep(1)
            result = await flow(s, auth, flow_id, {})
            if result["type"] != "progress":
                break
        assert result["type"] == "create_entry", result

        async with s.ws_connect(f"{HA}/api/websocket") as ws:
            await ws.receive_json()
            await ws.send_json({"type": "auth", "access_token": auth["Authorization"][7:]})
            assert (await ws.receive_json())["type"] == "auth_ok"
            await ws.send_json({"id": 1, "type": "config/entity_registry/list"})
            entries = [e for e in (await ws.receive_json())["result"] if e["platform"] == "deckconnector"]
            assert len(entries) == 11 + 2 * 5, sorted(e["entity_id"] for e in entries)
            controls = next(e["entity_id"] for e in entries if e["unique_id"] == "02005e000001_SIM0001_controls")

            await ws.send_json({"id": 2, "type": "subscribe_entities", "entity_ids": [controls]})
            await ws.receive_json()
            await ws.receive_json()
            press_file.touch()
            seen = []
            end = time.monotonic() + 10
            while len(seen) < 2 and time.monotonic() < end:
                msg = await ws.receive_json(timeout=10)
                for change in msg.get("event", {}).get("c", {}).values():
                    attrs = change.get("+", {}).get("a", {})
                    if attrs.get("event_type"):
                        seen.append(attrs["event_type"])
            assert seen == ["key_1_press", "key_1_release"], seen

        async with s.get(f"{HA}/api/states/{controls}", headers=auth) as r:
            assert (await r.json())["attributes"]["event_type"] == "key_1_release"


async def test_a_setting_reaches_the_board(press_file, ha):
    async with aiohttp.ClientSession() as s:
        auth = await login(s)
        entity = "number.deckconnector_5678ab_screen_off_after"
        async with s.get(f"{HA}/api/states/{entity}", headers=auth) as r:
            before = float((await r.json())["state"])
        body = {"entity_id": entity, "value": 15}
        async with s.post(f"{HA}/api/services/number/set_value", json=body, headers=auth) as r:
            assert r.status == 200, await r.text()
        board = aiohttp.BasicAuth("admin", "sim-pass")
        async with s.get(f"http://127.0.0.1:{SIM_PORT}/api/status", auth=board) as r:
            assert (await r.json())["screen_off_s"] == 900
        for _ in range(20):
            async with s.get(f"{HA}/api/states/{entity}", headers=auth) as r:
                if (await r.json())["state"] == "15.0":
                    break
            await asyncio.sleep(0.5)
        else:
            raise AssertionError("the pushed value did not reach Home Assistant")
        async with s.post(f"{HA}/api/services/number/set_value", json={**body, "value": before}, headers=auth) as r:
            assert r.status == 200


async def login(s: aiohttp.ClientSession) -> dict:
    body = {"client_id": CLIENT, "handler": ["homeassistant", None], "redirect_uri": CLIENT}
    async with s.post(f"{HA}/auth/login_flow", json=body) as r:
        flow_id = (await r.json())["flow_id"]
    body = {"client_id": CLIENT, "username": "test", "password": "test-pass-1"}
    async with s.post(f"{HA}/auth/login_flow/{flow_id}", json=body) as r:
        code = (await r.json())["result"]
    form = {"grant_type": "authorization_code", "code": code, "client_id": CLIENT}
    async with s.post(f"{HA}/auth/token", data=form) as r:
        return {"Authorization": f"Bearer {(await r.json())['access_token']}"}


async def test_homekit_bridges_the_deck_triggers(press_file, ha):
    async with aiohttp.ClientSession() as s:
        auth = await login(s)
        async with s.ws_connect(f"{HA}/api/websocket") as ws:
            await ws.receive_json()
            await ws.send_json({"type": "auth", "access_token": auth["Authorization"][7:]})
            await ws.receive_json()
            await ws.send_json({"id": 1, "type": "config/device_registry/list"})
            devices = (await ws.receive_json())["result"]
        deck = next(d["id"] for d in devices if ["deckconnector", "SIM0001"] in d["identifiers"])

        trigger = {
            "trigger": "device",
            "domain": "deckconnector",
            "device_id": deck,
            "type": "press",
            "subtype": "key_1",
        }
        automation = {"alias": "key 1", "triggers": [trigger], "actions": [{"delay": 0}]}
        async with s.post(f"{HA}/api/config/automation/config/key1", json=automation, headers=auth) as r:
            assert r.status == 200, await r.text()

        async def options(flow_id: str | None, body: dict) -> dict:
            url = f"{HA}/api/config/config_entries/options/flow" + (f"/{flow_id}" if flow_id else "")
            async with s.post(url, json=body, headers=auth) as r:
                assert r.status == 200, await r.text()
                return await r.json()

        result = await flow(s, auth, None, {"handler": "homekit"})
        result = await flow(s, auth, result["flow_id"], {"include_domains": ["lock"]})
        result = await flow(s, auth, result["flow_id"], {})
        entry = result["result"]["entry_id"]
        result = await options(None, {"handler": entry, "show_advanced_options": True})
        result = await options(
            result["flow_id"], {"mode": "bridge", "include_exclude_mode": "include", "domains": ["lock"]}
        )
        result = await options(result["flow_id"], {"entities": []})
        assert result["step_id"] == "bridged_device_triggers", result
        result = await options(result["flow_id"], {"devices": [deck]})
        assert result["type"] == "create_entry", result

        services: list = []
        for _ in range(60):
            await asyncio.sleep(1)
            async with s.get(f"{HA}/api/diagnostics/config_entry/{entry}", headers=auth) as r:
                diag = (await r.json())["data"]
            accessory = next(
                (a for a in diag.get("bridge", {}).values() if a["config"].get("platform") == "deckconnector"), None
            )
            if accessory:
                hap = next(a for a in diag["accessories"] if a["aid"] == accessory["aid"])
                services = [x["type"] for x in hap["services"]]
                break
        assert services.count("89") == 8 + 4 * 3 + 4 + 4 * 2, services  # keys, dials, strip, zones
        assert services.count("CC") == services.count("89")

        async with s.get(f"{HA}/api/states/automation.key_1", headers=auth) as r:
            assert (await r.json())["attributes"]["last_triggered"] is None
        press_file.touch()
        for _ in range(20):
            await asyncio.sleep(0.5)
            async with s.get(f"{HA}/api/states/automation.key_1", headers=auth) as r:
                if (await r.json())["attributes"]["last_triggered"]:
                    break
        else:
            raise AssertionError("the automation on key 1 did not run")
