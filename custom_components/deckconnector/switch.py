from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry
from .entity import BoardEntity

PARALLEL_UPDATES = 1


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([ScreenSwitch(entry.runtime_data, "screen")])


class ScreenSwitch(BoardEntity, SwitchEntity):
    @property
    def is_on(self) -> bool:
        return not self.coordinator.data.get("screen_off")

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self.call("POST", "/api/screen", {"on": True})

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.call("POST", "/api/screen", {"on": False})
