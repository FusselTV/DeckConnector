import json
from pathlib import Path

from homeassistant.components import automation
from homeassistant.components.device_automation import DeviceAutomationType, async_get_device_automations
from homeassistant.components.device_automation.trigger import async_validate_trigger_config
from homeassistant.helpers import device_registry as dr
from homeassistant.setup import async_setup_component
import pytest
from test_setup import setup

from conftest import PLUS, XL
from custom_components.deckconnector.device_trigger import trigger_of
from custom_components.deckconnector.event import event_types

TRANSLATIONS = Path(__file__).parents[3] / "custom_components/deckconnector/translations"


def device(hass, entry, ident):
    return dr.async_get(hass).async_get_device_by_identifier(("deckconnector", ident), entry.entry_id)


def trigger(device_id, type_, subtype):
    return {"trigger": "device", "domain": "deckconnector", "device_id": device_id, "type": type_, "subtype": subtype}


async def test_triggers_per_model(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS, XL)
    ids = {
        name: device(hass, entry, ident).id for name, ident in (("plus", "A1"), ("xl", "B2"), ("board", "a0b1c2a1b2c3"))
    }
    found = await async_get_device_automations(hass, DeviceAutomationType.TRIGGER, list(ids.values()))
    plus = [t for t in found[ids["plus"]] if t["domain"] == "deckconnector"]
    xl = [t for t in found[ids["xl"]] if t["domain"] == "deckconnector"]
    assert len(plus) == 8 + 4 * 3 + 4 + 2 * 4
    assert len(xl) == 32
    assert not [t for t in found[ids["board"]] if t["domain"] == "deckconnector"]
    assert {t["domain"] for t in found[ids["plus"]]} == {"deckconnector"}, "HomeKit would show the others as buttons"
    assert {"type": "turn_left", "subtype": "dial_4"}.items() <= next(
        t for t in plus if t["subtype"] == "dial_4" and t["type"] == "turn_left"
    ).items()
    assert {(t["type"], t["subtype"]) for t in plus} >= {("tap", "touch_4"), ("long_press", "touch_1")}
    for trigger in plus + xl:
        await async_validate_trigger_config(hass, trigger)


async def test_an_automation_runs_on_a_press(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    deck = device(hass, entry, "A1").id
    runs = []
    hass.services.async_register("test", "run", lambda call: runs.append(dict(call.data)))
    assert await async_setup_component(
        hass,
        automation.DOMAIN,
        {
            automation.DOMAIN: [
                {
                    "triggers": [
                        {
                            "trigger": "device",
                            "domain": "deckconnector",
                            "device_id": deck,
                            "type": "press",
                            "subtype": "key_3",
                        }
                    ],
                    "actions": [{"action": "test.run", "data": {"subtype": "{{ trigger.subtype }}"}}],
                },
                {
                    "triggers": [
                        {
                            "trigger": "device",
                            "domain": "deckconnector",
                            "device_id": deck,
                            "type": "turn_right",
                            "subtype": "dial_2",
                        }
                    ],
                    "actions": [{"action": "test.run", "data": {"ticks": "{{ trigger.ticks }}"}}],
                },
            ]
        },
    )
    press = {"t": "input", "kind": "key_down", "index": 2, "value": 0, "x": 0, "y": 0, "deck": "A1", "simulated": False}
    for msg in (
        {**press, "kind": "key_up"},
        {**press, "simulated": True},
        {**press, "index": 3},
        press,
        {**press, "kind": "encoder_turn", "index": 1, "value": 3},
    ):
        await socket.queue.put(msg)
    await hass.async_block_till_done()
    assert runs == [{"subtype": "key_3"}, {"ticks": 3}]


@pytest.mark.parametrize("lang", ["en", "de"])
def test_the_biggest_deck_has_a_name_for_every_control(lang):
    strings = json.loads((TRANSLATIONS / f"{lang}.json").read_text())
    names = strings["entity"]["event"]["controls"]["state_attributes"]["event_type"]["state"]
    subtypes = strings["device_automation"]["trigger_subtype"]
    types = event_types({"keys": 40, "encoders": 6, "touch": True})  # Mirabox decks report up to 40 keys
    assert set(types) <= names.keys()
    assert {trigger_of(t)[1] for t in types if trigger_of(t)} | {f"touch_{n}" for n in range(1, 7)} <= subtypes.keys()


async def test_a_tap_runs_the_automations_of_its_zone_and_of_the_whole_strip(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    deck = device(hass, entry, "A1").id
    runs = []
    hass.services.async_register("test", "run", lambda call: runs.append(dict(call.data)))
    assert await async_setup_component(
        hass,
        automation.DOMAIN,
        {
            automation.DOMAIN: [
                {
                    "triggers": [trigger(deck, type_, subtype)],
                    "actions": [{"action": "test.run", "data": {"on": subtype, "zone": "{{ trigger.zone }}"}}],
                }
                for type_, subtype in (("tap", "touch_2"), ("tap", "touch_strip"), ("long_press", "touch_4"))
            ]
        },
    )
    tap = {"t": "input", "kind": "touch_tap", "index": 0, "value": 0, "x": 250, "y": 420, "deck": "A1"}
    for msg in (tap, {**tap, "x": 799}, {**tap, "kind": "touch_long", "x": 799}, {**tap, "simulated": True}):
        await socket.queue.put(msg)
    await hass.async_block_till_done()
    assert sorted((r["on"], r["zone"]) for r in runs) == [
        ("touch_2", 2),
        ("touch_4", 4),
        ("touch_strip", 2),
        ("touch_strip", 4),
    ]
