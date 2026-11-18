from __future__ import annotations

from homeassistant.components.select import SelectEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry
from .entity import BoardEntity

PARALLEL_UPDATES = 1

CONTROLLERS = ["companion", "app", "server"]


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([Controller(entry.runtime_data, "controller")])


class Controller(BoardEntity, SelectEntity):
    _attr_entity_category = EntityCategory.CONFIG
    _attr_options = CONTROLLERS

    @property
    def current_option(self) -> str | None:
        return self.coordinator.data.get("controller")

    async def async_select_option(self, option: str) -> None:
        await self.call("PUT", "/api/config", {"controller": option, "host": ""})
