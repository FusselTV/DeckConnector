from __future__ import annotations

from homeassistant.components.number import NumberEntity
from homeassistant.const import PERCENTAGE
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry
from .entity import BoardEntity

PARALLEL_UPDATES = 1


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([Brightness(entry.runtime_data, "brightness")])


class Brightness(BoardEntity, NumberEntity):
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_native_min_value = 0
    _attr_native_max_value = 100
    _attr_native_step = 1

    @property
    def native_value(self) -> int | None:
        return self.coordinator.data.get("brightness")

    async def async_set_native_value(self, value: float) -> None:
        await self.call("PUT", "/api/config", {"brightness": round(value)})
