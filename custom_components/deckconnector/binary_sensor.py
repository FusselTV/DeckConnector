from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.binary_sensor import BinarySensorEntity, BinarySensorEntityDescription
from homeassistant.components.binary_sensor.const import BinarySensorDeviceClass
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity, DeckEntity, add_per_deck

PARALLEL_UPDATES = 0


@dataclass(frozen=True, kw_only=True)
class BoardBinaryDescription(BinarySensorEntityDescription):
    value: Callable[[dict[str, Any]], bool | None]


def _net(data: dict[str, Any], iface: str) -> dict[str, Any]:
    return dict((data.get("network") or {}).get(iface) or {})


BOARD = (
    BoardBinaryDescription(
        key="ethernet",
        device_class=BinarySensorDeviceClass.CONNECTIVITY,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda d: bool(_net(d, "lan").get("cable")) and not _net(d, "lan").get("dead"),
    ),
    BoardBinaryDescription(
        key="backup_wifi",
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda d: _net(d, "wlan").get("backup"),
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    if "network" in coordinator.data:
        async_add_entities(BoardBinary(coordinator, d) for d in BOARD)
    add_per_deck(entry, async_add_entities, lambda c, d: [Unlocked(c, d)])


class BoardBinary(BoardEntity, BinarySensorEntity):
    entity_description: BoardBinaryDescription

    def __init__(self, coordinator: DeckCoordinator, description: BoardBinaryDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value(self.coordinator.data)


class Unlocked(DeckEntity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.LOCK
    _attr_entity_registry_enabled_default = False  # its device triggers would become buttons in a HomeKit bridge

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any]) -> None:
        super().__init__(coordinator, deck, "pin_lock")

    @property
    def is_on(self) -> bool | None:
        deck = self.deck
        return None if deck is None or "locked" not in deck else not deck["locked"]
