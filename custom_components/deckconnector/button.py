from __future__ import annotations

from typing import Any

from homeassistant.components.button import ButtonEntity
from homeassistant.components.button.const import ButtonDeviceClass
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import Entity
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity, DeckEntity, add_per_deck

PARALLEL_UPDATES = 1


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    board: list[Entity] = [Restart(coordinator, "restart")]
    if coordinator.data.get("speaker"):
        board.append(Find(coordinator, "find"))
    async_add_entities(board)
    add_per_deck(entry, async_add_entities, lambda c, d: [Reconnect(c, d)])


class Restart(BoardEntity, ButtonEntity):
    _attr_device_class = ButtonDeviceClass.RESTART
    _attr_entity_category = EntityCategory.CONFIG

    async def async_press(self) -> None:
        await self.call("POST", "/api/reboot")


class Find(BoardEntity, ButtonEntity):
    _attr_device_class = ButtonDeviceClass.IDENTIFY

    async def async_press(self) -> None:
        await self.call("POST", "/api/sound", {"sound": "find"})


class Reconnect(DeckEntity, ButtonEntity):
    _attr_entity_category = EntityCategory.CONFIG
    _attr_entity_registry_enabled_default = False

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any]) -> None:
        super().__init__(coordinator, deck, "reconnect")

    async def async_press(self) -> None:
        await self.call("POST", f"/api/deck/reconnect?deck={self.serial}")
