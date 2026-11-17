from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr, entity_registry as er
import pytest

from conftest import HOST, PLUS, XL, status


async def setup(hass, entry, aioclient_mock, socket, *decks):
    aioclient_mock.get(f"http://{HOST}/api/status", json=status(*decks))
    aioclient_mock.get(f"http://{HOST}/api/update/check", json={"current": "v1.0.0", "latest": None})
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    await socket.queue.put({"t": "state", **status(*decks)})
    await hass.async_block_till_done()


def entities(hass, entry):
    return er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)


@pytest.mark.parametrize(
    ("decks", "count"),
    [((), 8), ((PLUS,), 11), ((PLUS, XL), 14)],
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
    assert hass.states.get("sensor.stream_deck_host").state == "companion"
    assert hass.states.get("sensor.stream_deck_xl_host").state == "board"
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


async def test_update_shows_the_progress_from_the_socket(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    entity = "update.deckconnector_a1b2c3_firmware"
    assert hass.states.get(entity).attributes["in_progress"] is False
    await socket.queue.put({"t": "ota", "target": "p4", "state": "download", "pct": 42})
    await hass.async_block_till_done()
    state = hass.states.get(entity)
    assert state.attributes["in_progress"] is True and state.attributes["update_percentage"] == 42
    assert "entity_picture" not in state.attributes
