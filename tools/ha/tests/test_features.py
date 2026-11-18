import logging
from unittest.mock import patch

from homeassistant.components.diagnostics import REDACTED
from homeassistant.const import STATE_OFF, STATE_ON
from homeassistant.helpers import device_registry as dr, entity_registry as er, issue_registry as ir
from homeassistant.setup import async_setup_component
import pytest
from pytest_homeassistant_custom_component.common import get_system_health_info
from pytest_homeassistant_custom_component.components.diagnostics import get_diagnostics_for_config_entry
from test_setup import CHECK, entities, setup

from conftest import HOST, PLUS, STATUS, XL, status
from custom_components.deckconnector.entity import maker

BOARD = "deckconnector_a1b2c3"


async def push(hass, socket, **changes):
    await socket.queue.put({"t": "state", **status(PLUS), **changes})
    await hass.async_block_till_done()


async def test_board_settings_show_and_go_to_the_board(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    assert hass.states.get(f"number.{BOARD}_screen_off_after").state == "10.0"
    assert hass.states.get(f"number.{BOARD}_volume").state == "30"
    aioclient_mock.put(f"http://{HOST}/api/config", json={"ok": True})
    await hass.services.async_call(
        "number", "set_value", {"entity_id": f"number.{BOARD}_screen_off_after", "value": 0}, blocking=True
    )
    await hass.services.async_call(
        "number", "set_value", {"entity_id": f"number.{BOARD}_volume", "value": 55}, blocking=True
    )
    assert [c[2] for c in aioclient_mock.mock_calls[-2:]] == [{"screen_off_s": 0}, {"volume": 55}]


async def test_buttons_reach_their_routes(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.post(f"http://{HOST}/api/sound", json={"ok": True})
    aioclient_mock.post(f"http://{HOST}/api/reboot", json={"ok": True})
    await hass.services.async_call("button", "press", {"entity_id": f"button.{BOARD}_find"}, blocking=True)
    await hass.services.async_call("button", "press", {"entity_id": f"button.{BOARD}_restart"}, blocking=True)
    assert aioclient_mock.mock_calls[-2][2] == {"sound": "find"}
    assert str(aioclient_mock.mock_calls[-1][1]) == f"http://{HOST}/api/reboot"


@pytest.mark.usefixtures("entity_registry_enabled_by_default")
async def test_reconnect_names_the_deck(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.post(f"http://{HOST}/api/deck/reconnect?deck=A1", json={"ok": True})
    await hass.services.async_call("button", "press", {"entity_id": "button.stream_deck_reconnect"}, blocking=True)
    assert str(aioclient_mock.mock_calls[-1][1]).endswith("/api/deck/reconnect?deck=A1")


async def test_disabled_by_default(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    disabled = {e.entity_id for e in entities(hass, entry) if e.disabled_by}
    assert disabled == {
        f"sensor.{BOARD}_wi_fi_signal",
        f"sensor.{BOARD}_wi_fi_network",
        f"sensor.{BOARD}_last_restart",
        f"update.{BOARD}_firmware",
        "button.stream_deck_reconnect",
        "binary_sensor.stream_deck_pin_lock",
    }


@pytest.mark.usefixtures("entity_registry_enabled_by_default")
async def test_network_and_pin_lock(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    assert hass.states.get(f"binary_sensor.{BOARD}_ethernet").state == STATE_ON
    assert hass.states.get(f"binary_sensor.{BOARD}_backup_wi_fi").state == STATE_OFF
    assert hass.states.get("binary_sensor.stream_deck_pin_lock").state == STATE_ON
    locked = status(PLUS)
    locked["decks"][0]["locked"] = True
    locked["network"] = {"lan": {"cable": True, "dead": True}, "wlan": {"backup": True}}
    await socket.queue.put({"t": "state", **locked})
    await hass.async_block_till_done()
    assert hass.states.get(f"binary_sensor.{BOARD}_ethernet").state == STATE_OFF
    assert hass.states.get(f"binary_sensor.{BOARD}_backup_wi_fi").state == STATE_ON
    assert hass.states.get("binary_sensor.stream_deck_pin_lock").state == STATE_OFF


@pytest.mark.usefixtures("entity_registry_enabled_by_default")
async def test_older_firmware_gets_no_entities_it_cannot_feed(hass, entry, aioclient_mock, socket):
    old = {k: v for k, v in status(PLUS).items() if k not in ("volume", "screen_off_s", "speaker", "network")}
    old["decks"][0].pop("locked")
    aioclient_mock.get(f"http://{HOST}/api/status", json=old)
    aioclient_mock.get(CHECK, json={"latest": None})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    await socket.queue.put({"t": "state", **old})
    await hass.async_block_till_done()
    ids = {e.entity_id for e in entities(hass, entry)}
    assert not {i for i in ids if "volume" in i or "screen_off" in i or "find" in i or "ethernet" in i}
    assert hass.states.get("binary_sensor.stream_deck_pin_lock").state == "unknown"


async def test_decks_over_the_limit_raise_a_repair(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    issues = ir.async_get(hass)
    issue_id = f"decks_over_limit_{entry.entry_id}"
    await push(hass, socket, decks_over_limit=1)
    issue = issues.async_get_issue("deckconnector", issue_id)
    assert issue and issue.translation_placeholders == {"name": STATUS["name"], "count": "1", "max": "2"}
    await push(hass, socket, decks_over_limit=0)
    assert issues.async_get_issue("deckconnector", issue_id) is None
    await push(hass, socket, decks_over_limit=2)
    assert await hass.config_entries.async_remove(entry.entry_id)
    assert issues.async_get_issue("deckconnector", issue_id) is None


async def test_https_only_raises_a_repair_until_the_board_answers(hass, entry, aioclient_mock, socket):
    aioclient_mock.get(f"http://{HOST}/api/status", status=403, text="HTTPS only")
    entry.add_to_hass(hass)
    assert not await hass.config_entries.async_setup(entry.entry_id)
    issue_id = f"https_only_{entry.entry_id}"
    assert ir.async_get(hass).async_get_issue("deckconnector", issue_id)
    aioclient_mock.clear_requests()
    aioclient_mock.get(f"http://{HOST}/api/status", json=status(PLUS))
    aioclient_mock.get(CHECK, json={"latest": None})
    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()
    assert ir.async_get(hass).async_get_issue("deckconnector", issue_id) is None


async def test_a_rejected_key_starts_reauth(hass, entry, aioclient_mock, socket):
    aioclient_mock.get(f"http://{HOST}/api/status", status=401, json={"error": "no"})
    entry.add_to_hass(hass)
    assert not await hass.config_entries.async_setup(entry.entry_id)
    assert [f["context"]["source"] for f in hass.config_entries.flow.async_progress()] == ["reauth"]


async def test_a_new_firmware_reloads_the_entry(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    with patch.object(hass.config_entries, "async_schedule_reload") as reload:
        await push(hass, socket, firmware="v1.1.0")
    reload.assert_called_once_with(entry.entry_id)


async def test_the_connection_is_logged_once_lost_and_once_back(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    caplog.set_level(logging.INFO, "custom_components.deckconnector")
    await socket.queue.put(None)
    await hass.async_block_till_done()
    await socket.opened.wait()
    await push(hass, socket)
    await push(hass, socket)
    lines = [r.getMessage() for r in caplog.records if HOST in r.getMessage()]
    assert lines == [f"lost the connection to {HOST}", f"connected to {HOST} again"]


async def test_diagnostics_hide_the_key_and_addresses(hass, hass_client, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.get(f"http://{HOST}/api/events", json=[{"kind": "boot", "text": "power"}])
    assert await async_setup_component(hass, "diagnostics", {})
    diag = await get_diagnostics_for_config_entry(hass, hass_client, entry)
    assert diag["entry"] == {"host": REDACTED, "token": REDACTED}
    assert diag["status"]["id"] == REDACTED and diag["status"]["decks"][0]["serial"] == REDACTED
    assert diag["status"]["network"]["wlan"]["ssid"] == REDACTED
    assert diag["events"] == [{"kind": "boot", "text": "power"}] and diag["connected"] is True


async def test_system_health(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS, XL)
    assert await async_setup_component(hass, "system_health", {})
    info = await get_system_health_info(hass, "deckconnector")
    assert info == {"boards": 1, "connected": 1, "decks": 2, "firmware": "v1.0.0"}


async def test_only_detached_devices_can_be_deleted(hass, hass_ws_client, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS, XL)
    assert await async_setup_component(hass, "config", {})
    devices = dr.async_get(hass)
    xl = devices.async_get_device_by_identifier(("deckconnector", "B2"), entry.entry_id)
    board = devices.async_get_device_by_identifier(("deckconnector", STATUS["id"]), entry.entry_id)
    client = await hass_ws_client(hass)

    async def remove(device_id: str) -> bool:
        await client.send_json_auto_id(
            {
                "type": "config/device_registry/remove_config_entry",
                "config_entry_id": entry.entry_id,
                "device_id": device_id,
            }
        )
        return (await client.receive_json())["success"]

    assert not await remove(xl.id)
    assert not await remove(board.id)
    await socket.queue.put({"t": "state", **status(PLUS)})
    await hass.async_block_till_done()
    assert await remove(xl.id)
    assert devices.async_get(xl.id) is None
    assert not er.async_entries_for_device(er.async_get(hass), xl.id)


def test_maker():
    assert [maker(m) for m in ("Stream Deck +", "Stream Dock N4", "Ajazz AKP153", "Soomfon CN002")] == [
        "Elgato",
        "Mirabox",
        "Ajazz",
        "Soomfon",
    ]
