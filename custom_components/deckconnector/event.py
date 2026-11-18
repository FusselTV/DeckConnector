from __future__ import annotations

from typing import Any

from homeassistant.components.event import EventDeviceClass, EventEntity
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import SIGNAL_INPUT
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import DeckEntity, add_per_deck

PARALLEL_UPDATES = 0


def event_types(deck: dict[str, Any]) -> list[str]:
    types = [f"key_{n}_{a}" for n in range(1, deck["keys"] + 1) for a in ("press", "release")]
    types += [f"dial_{n}_{a}" for n in range(1, deck["encoders"] + 1) for a in ("press", "release", "left", "right")]
    if deck["encoders"]:
        types += ["touch_tap", "touch_long_press", "touch_swipe_left", "touch_swipe_right"]
    return types


def event_of(msg: dict[str, Any]) -> tuple[str, dict[str, int]] | None:
    n = msg["index"] + 1
    match msg["kind"]:
        case "key_down":
            return f"key_{n}_press", {}
        case "key_up":
            return f"key_{n}_release", {}
        case "encoder_down":
            return f"dial_{n}_press", {}
        case "encoder_up":
            return f"dial_{n}_release", {}
        case "encoder_turn":
            ticks = msg["value"]
            return f"dial_{n}_{'right' if ticks > 0 else 'left'}", {"ticks": abs(ticks)}
        case "touch_tap":
            return "touch_tap", {"x": msg["x"], "y": msg["y"]}
        case "touch_long":
            return "touch_long_press", {"x": msg["x"], "y": msg["y"]}
        case "touch_swipe":
            side = "left" if msg["x2"] < msg["x"] else "right"
            return f"touch_swipe_{side}", {k: msg[k] for k in ("x", "y", "x2", "y2")}
    return None


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    add_per_deck(entry, async_add_entities, lambda c, d: [ControlsEvent(c, d)])


class ControlsEvent(DeckEntity, EventEntity):
    _attr_device_class = EventDeviceClass.BUTTON

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any]) -> None:
        super().__init__(coordinator, deck, "controls")
        self._attr_event_types = event_types(deck)

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(async_dispatcher_connect(self.hass, SIGNAL_INPUT.format(self.serial), self._input))

    @callback
    def _input(self, msg: dict[str, Any]) -> None:
        event = event_of(msg)
        if event and event[0] in self.event_types:
            self._trigger_event(*event)
            self.async_write_ha_state()
