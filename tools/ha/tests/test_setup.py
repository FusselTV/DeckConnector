import asyncio
from datetime import timedelta

from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr, entity_registry as er
from homeassistant.util import dt as dt_util
import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry, async_fire_time_changed

from conftest import HOST, PLUS, STATUS, TOKEN, XL, status

CHECK = f"http://{HOST}/api/update/check"


async def setup(hass, entry, aioclient_mock, socket, *decks):
    aioclient_mock.get(f"http://{HOST}/api/status", json=status(*decks))
    aioclient_mock.get(CHECK, json={"current": "v1.0.0", "latest": None})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    await socket.queue.put({"t": "state", **status(*decks)})
    await hass.async_block_till_done()


def firmware_on(hass) -> str:
    """The update entity starts off (it asks GitHub); a user turns it on."""
    reg = er.async_get(hass)
    return reg.async_get_or_create("update", "deckconnector", f"{STATUS['id']}_firmware").entity_id


def entities(hass, entry):
    return er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)


@pytest.mark.parametrize(
    ("decks", "count"),
    [((), 15), ((PLUS,), 20), ((PLUS, XL), 25)],
)
async def test_entity_count(hass: HomeAssistant, entry, aioclient_mock, socket, decks, count):
    await setup(hass, entry, aioclient_mock, socket, *decks)
    assert len(entities(hass, entry)) == count


async def test_decks_are_devices_behind_the_board(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS, XL)
    devices = dr.async_get(hass)
    board = devices.async_get_device_by_identifier(("deckconnector", "a0b1c2a1b2c3"), entry.entry_id)
    xl = devices.async_get_device_by_identifier(("deckconnector", "B2"), entry.entry_id)
    assert board.sw_version == "v1.0.0"
    assert xl.via_device_id == board.id and xl.model == "Stream Deck XL" and xl.serial_number == "B2"
    plus = hass.states.get("event.stream_deck_controls")
    xl_events = hass.states.get("event.stream_deck_xl_controls")
    assert len(plus.attributes["event_types"]) == 8 * 2 + 4 * 4 + 4
    assert len(xl_events.attributes["event_types"]) == 32 * 2
    assert hass.states.get("sensor.stream_deck_source").state == "companion"
    assert hass.states.get("sensor.stream_deck_xl_source").state == "board"
    assert hass.states.get("number.deckconnector_a1b2c3_brightness").state == "80"
    assert hass.states.get("switch.deckconnector_a1b2c3_screen").state == "on"
    assert hass.states.get("select.deckconnector_a1b2c3_controller").state == "companion"
    assert hass.states.get("sensor.deckconnector_a1b2c3_temperature").state == "41"


async def test_a_press_fires_the_event(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    entity = "event.stream_deck_controls"
    assert hass.states.get(entity).state == STATE_UNKNOWN
    press = {"t": "input", "kind": "key_down", "index": 2, "value": 0, "x": 0, "y": 0, "deck": "A1"}
    await socket.queue.put({**press, "simulated": True})
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == STATE_UNKNOWN
    await socket.queue.put({**press, "simulated": False})
    await hass.async_block_till_done()
    assert hass.states.get(entity).attributes["event_type"] == "key_3_press"
    await socket.queue.put({**press, "kind": "encoder_turn", "index": 0, "value": -2, "simulated": False})
    await hass.async_block_till_done()
    state = hass.states.get(entity)
    assert state.attributes["event_type"] == "dial_1_left" and state.attributes["ticks"] == 2


async def test_unavailable_while_the_socket_is_down(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    entity = "number.deckconnector_a1b2c3_brightness"
    assert hass.states.get(entity).state == "80"
    await socket.queue.put(None)
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == STATE_UNAVAILABLE
    assert hass.states.get("event.stream_deck_controls").state == STATE_UNAVAILABLE
    await socket.opened.wait()
    await socket.queue.put({"t": "state", **status(PLUS), "brightness": 30})
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "30"


async def test_unplugged_deck_goes_unavailable_and_a_new_one_appears(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await socket.queue.put({"t": "state", **status(XL)})
    await hass.async_block_till_done()
    assert hass.states.get("event.stream_deck_controls").state == STATE_UNAVAILABLE
    assert hass.states.get("event.stream_deck_xl_controls").state == STATE_UNKNOWN


async def test_brightness_goes_to_the_board(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.put(f"http://{HOST}/api/config", json={"ok": True})
    await hass.services.async_call(
        "number", "set_value", {"entity_id": "number.deckconnector_a1b2c3_brightness", "value": 40}, blocking=True
    )
    assert aioclient_mock.mock_calls[-1][2] == {"brightness": 40}


async def test_the_update_entity_starts_off(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    entity = er.async_get(hass).async_get("update.deckconnector_a1b2c3_firmware")
    assert entity.disabled_by is er.RegistryEntryDisabler.INTEGRATION
    await spin(hass)
    assert calls(aioclient_mock, CHECK) == 0


async def test_update_shows_the_progress_from_the_socket(hass, entry, aioclient_mock, socket):
    entity = firmware_on(hass)
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    assert hass.states.get(entity).attributes["in_progress"] is False
    await socket.queue.put({"t": "ota", "target": "p4", "state": "download", "pct": 42})
    await hass.async_block_till_done()
    state = hass.states.get(entity)
    assert state.attributes["in_progress"] is True and state.attributes["update_percentage"] == 42


async def spin(hass):
    for _ in range(5):
        await hass.async_block_till_done()
        await asyncio.sleep(0)


def calls(aioclient_mock, url):
    return sum(1 for c in aioclient_mock.mock_calls if str(c[1]) == url)


async def test_update_check_runs_after_setup_and_every_6_hours(hass, entry, aioclient_mock, socket):
    firmware_on(hass)
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await spin(hass)
    assert calls(aioclient_mock, CHECK) == 1
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(hours=6, seconds=1))
    await spin(hass)
    assert calls(aioclient_mock, CHECK) == 2


async def test_polled_values_survive_a_state_push(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    coordinator = entry.runtime_data
    assert coordinator.data["network"]["wlan"]["rssi"] == -48
    pushed = status(PLUS)
    pushed["network"] = {"wlan": {"ssid": "studio"}}
    del pushed["temp_c"]
    await socket.queue.put({"t": "state", **pushed})
    await hass.async_block_till_done()
    assert coordinator.data["network"]["wlan"] == {"rssi": -48, "ssid": "studio", "backup": False}
    assert hass.states.get("sensor.deckconnector_a1b2c3_temperature").state == "41"


async def test_a_failed_poll_keeps_entities_while_the_socket_is_up(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.clear_requests()
    aioclient_mock.get(f"http://{HOST}/api/status", exc=TimeoutError)
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=61))
    await hass.async_block_till_done()
    assert calls(aioclient_mock, f"http://{HOST}/api/status") == 1
    assert hass.states.get("number.deckconnector_a1b2c3_brightness").state == "80"


async def test_the_same_deck_on_two_boards_gets_entities_on_both(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    other = MockConfigEntry(
        domain="deckconnector", unique_id="0011223344ff", title="other", data={"host": "192.0.2.20", "token": TOKEN}
    )
    aioclient_mock.get("http://192.0.2.20/api/status", json={**status(PLUS), "id": "0011223344ff", "name": "other"})
    aioclient_mock.get("http://192.0.2.20/api/update/check", json={"current": "v1.0.0", "latest": None})
    other.add_to_hass(hass)
    assert await hass.config_entries.async_setup(other.entry_id)
    await hass.async_block_till_done()
    assert len(entities(hass, entry)) == 20
    assert len(entities(hass, other)) == 20


async def test_a_deck_in_another_slot_keeps_its_entities(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    before = {e.entity_id for e in entities(hass, entry)}
    await socket.queue.put({"t": "state", **status(XL, PLUS)})
    await hass.async_block_till_done()
    assert before <= {e.entity_id for e in entities(hass, entry)}
    assert hass.states.get("event.stream_deck_controls").state == STATE_UNKNOWN
    assert len(entities(hass, entry)) == 25


async def test_a_refused_command_is_a_translated_error(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.post(f"http://{HOST}/api/screen", status=503, json={"ok": False, "error": "busy"})
    with pytest.raises(HomeAssistantError) as err:
        await hass.services.async_call(
            "switch", "turn_off", {"entity_id": "switch.deckconnector_a1b2c3_screen"}, blocking=True
        )
    assert err.value.translation_key == "command_failed"
    assert err.value.translation_placeholders == {"name": STATUS["name"], "error": "busy"}


async def test_removing_the_entry_ends_its_key_on_the_board(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.delete(f"http://{HOST}/api/token", json={"ok": True})
    assert await hass.config_entries.async_remove(entry.entry_id)
    sent = [(m, str(u), h) for m, u, _, h in aioclient_mock.mock_calls if m == "DELETE"]
    assert sent == [("DELETE", f"http://{HOST}/api/token", {"Authorization": f"Bearer {TOKEN}"})]


async def test_a_reload_keeps_the_key(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    assert await hass.config_entries.async_reload(entry.entry_id)
    await hass.async_block_till_done()
    assert not [c for c in aioclient_mock.mock_calls if c[0] == "DELETE"]
