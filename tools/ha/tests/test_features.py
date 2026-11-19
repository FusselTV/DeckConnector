import logging
from unittest.mock import patch

from homeassistant.components.diagnostics import REDACTED
from homeassistant.config_entries import ConfigEntryDisabler
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


async def test_the_screen_saver_dims_to_a_level_in_tens(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    entity = f"number.{BOARD}_screen_saver_brightness"
    assert hass.states.get(entity).state == "0"
    assert hass.states.get(entity).attributes["step"] == 10
    aioclient_mock.put(f"http://{HOST}/api/config", json={"ok": True})
    await hass.services.async_call("number", "set_value", {"entity_id": entity, "value": 30}, blocking=True)
    assert aioclient_mock.mock_calls[-1][2] == {"screen_dim": 30}
    await push(hass, socket, screen_dim=30)
    assert hass.states.get(entity).state == "30"


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
    assert await hass.config_entries.async_unload(entry.entry_id)
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


@pytest.mark.parametrize("how", ["disable", "remove"])
async def test_the_https_repair_ends_with_a_failed_entry(hass, entry, aioclient_mock, socket, how):
    assert await async_setup_component(hass, "deckconnector", {})
    aioclient_mock.get(f"http://{HOST}/api/status", status=403, text="HTTPS only")
    entry.add_to_hass(hass)
    assert not await hass.config_entries.async_setup(entry.entry_id)
    issue_id = f"https_only_{entry.entry_id}"
    assert ir.async_get(hass).async_get_issue("deckconnector", issue_id)
    if how == "disable":
        await hass.config_entries.async_set_disabled_by(entry.entry_id, ConfigEntryDisabler.USER)
    else:
        aioclient_mock.delete(f"http://{HOST}/api/token", status=403, text="HTTPS only")
        await hass.config_entries.async_remove(entry.entry_id)
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


@pytest.mark.parametrize("paged", [True, False])  # firmware since event codes, and before
async def test_diagnostics_hide_the_key_and_addresses(hass, hass_client, entry, aioclient_mock, socket, paged):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    events = [
        {"kind": "pairing", "code": "pair_accepted", "args": ["192.168.1.15"], "text": "192.168.1.15 accepted"},
        {"kind": "wlan", "code": "wlan_back", "args": ["studio", "-50", "x"], "text": "WLAN studio is back"},
    ]
    aioclient_mock.get(f"http://{HOST}/api/events", json={"events": events} if paged else events)
    network = {"lan": {"ip": "192.168.1.50"}, "wlan": {"ssid": "studio", "bssid": "aa:bb", "lock": "aa:bb"}}
    source = {"kind": "app", "host": "192.168.1.20", "name": "studio-pc", "deck": 0}
    await push(hass, socket, network=network, sources=[source], pairing={"hosts": [{"ip": "x", "mac": "y"}]})
    assert await async_setup_component(hass, "diagnostics", {})
    diag = await get_diagnostics_for_config_entry(hass, hass_client, entry)
    st = diag["status"]
    assert diag["entry"] == {"host": REDACTED, "token": REDACTED}
    assert st["id"] == REDACTED and st["decks"][0]["serial"] == REDACTED
    assert st["network"]["lan"]["ip"] == REDACTED
    assert {st["network"]["wlan"][k] for k in ("ssid", "bssid", "lock")} == {REDACTED}
    assert st["sources"][0]["host"] == REDACTED and st["pairing"]["hosts"][0] == {"ip": REDACTED, "mac": REDACTED}
    assert [e["text"] for e in diag["events"]] == [f"{REDACTED} accepted", f"WLAN {REDACTED} is back"]
    assert diag["events"][0]["args"] == [REDACTED] and diag["events"][1]["args"][0] == REDACTED
    assert st["sources"][0]["name"] == REDACTED and diag["connected"] is True


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
    assert [maker(m) for m in ("Stream Deck +", "Stream Dock N4", "Ajazz AKP153", "Soomfon CN002", "")] == [
        "Elgato",
        "Mirabox",
        "Ajazz",
        "Soomfon",
        None,
    ]


async def test_a_bad_message_keeps_the_socket(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await socket.queue.put({"t": "state", **status(PLUS), "decks": [{"no": "serial"}]})
    await hass.async_block_till_done()
    assert "KeyError: 'serial'" in caplog.text
    await push(hass, socket, brightness=20)
    assert hass.states.get(f"number.{BOARD}_brightness").state == "20"


async def test_a_status_that_is_no_object_cannot_connect(hass, entry, aioclient_mock, socket):
    aioclient_mock.get(f"http://{HOST}/api/status", json=["x"])
    entry.add_to_hass(hass)
    assert not await hass.config_entries.async_setup(entry.entry_id)
    assert entry.state.name == "SETUP_RETRY"


async def test_the_same_or_a_missing_firmware_does_not_reload(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    with patch.object(hass.config_entries, "async_schedule_reload") as reload:
        await push(hass, socket, firmware=None)
        await push(hass, socket)
    reload.assert_not_called()


async def test_https_only_while_running(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.clear_requests()
    aioclient_mock.get(f"http://{HOST}/api/status", status=403, text="HTTPS only")
    await entry.runtime_data.async_refresh()
    issue_id = f"https_only_{entry.entry_id}"
    assert ir.async_get(hass).async_get_issue("deckconnector", issue_id)
    assert hass.states.get(f"number.{BOARD}_brightness").state == "80"
    aioclient_mock.delete(f"http://{HOST}/api/token", status=403, text="HTTPS only")
    assert await hass.config_entries.async_remove(entry.entry_id)
    assert ir.async_get(hass).async_get_issue("deckconnector", issue_id) is None


async def test_diagnostics_say_why_the_events_are_missing(hass, hass_client, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.get(f"http://{HOST}/api/events", status=500, json={"error": "busy"})
    assert await async_setup_component(hass, "diagnostics", {})
    diag = await get_diagnostics_for_config_entry(hass, hass_client, entry)
    assert diag["events"] == "not read: busy"
