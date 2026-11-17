from __future__ import annotations

from collections.abc import Callable, Iterable
from typing import Any

from homeassistant.core import callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity import Entity
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .api import DeckConnectorError
from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator


class BoardEntity(CoordinatorEntity[DeckCoordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: DeckCoordinator, key: str) -> None:
        super().__init__(coordinator)
        self._attr_translation_key = key
        self._attr_unique_id = f"{coordinator.board_id}_{key}"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, coordinator.board_id)})

    @property
    def available(self) -> bool:
        return super().available and self.coordinator.connected

    async def call(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        try:
            return await self.coordinator.api.request(method, path, body)
        except DeckConnectorError as err:
            raise HomeAssistantError(f"{self.coordinator.data['name']}: {err}") from err


class DeckEntity(CoordinatorEntity[DeckCoordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any], key: str) -> None:
        super().__init__(coordinator)
        self.serial: str = deck["serial"]
        self._attr_translation_key = key
        self._attr_unique_id = f"{self.serial}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, self.serial)},
            via_device_id=coordinator.board_device_id,
            name=deck["model"],
            manufacturer="Elgato",
            model=deck["model"],
            serial_number=self.serial,
            sw_version=deck.get("firmware"),
        )

    @property
    def deck(self) -> dict[str, Any] | None:
        return self.coordinator.deck(self.serial)

    @property
    def available(self) -> bool:
        return super().available and self.coordinator.connected and self.deck is not None


def add_per_deck(
    entry: DeckConnectorConfigEntry,
    add: AddConfigEntryEntitiesCallback,
    make: Callable[[DeckCoordinator, dict[str, Any]], Iterable[Entity]],
) -> None:
    coordinator = entry.runtime_data
    known: set[str] = set()

    @callback
    def new_decks() -> None:
        for deck in coordinator.decks:
            if deck["serial"] not in known:
                known.add(deck["serial"])
                add(make(coordinator, deck))

    new_decks()
    entry.async_on_unload(coordinator.async_add_listener(new_decks))
