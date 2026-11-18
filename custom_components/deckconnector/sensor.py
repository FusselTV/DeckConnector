from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import SIGNAL_STRENGTH_DECIBELS_MILLIWATT, EntityCategory, UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity, DeckEntity, add_per_deck

PARALLEL_UPDATES = 0

HOSTS = ["companion", "app", "server", "board"]


@dataclass(frozen=True, kw_only=True)
class BoardSensorDescription(SensorEntityDescription):
    value: Callable[[dict[str, Any]], Any]


def _boot(data: dict[str, Any]) -> datetime | None:
    at = (data.get("boot") or {}).get("at")
    return datetime.fromtimestamp(at, UTC) if at else None


BOARD_SENSORS = (
    BoardSensorDescription(
        key="temperature",
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda d: d.get("temp_c"),
    ),
    BoardSensorDescription(
        key="wifi_signal",
        device_class=SensorDeviceClass.SIGNAL_STRENGTH,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=SIGNAL_STRENGTH_DECIBELS_MILLIWATT,
        entity_category=EntityCategory.DIAGNOSTIC,
        entity_registry_enabled_default=False,
        value=lambda d: d.get("network", {}).get("wlan", {}).get("rssi") or None,
    ),
    BoardSensorDescription(
        key="last_restart",
        device_class=SensorDeviceClass.TIMESTAMP,
        entity_category=EntityCategory.DIAGNOSTIC,
        entity_registry_enabled_default=False,
        value=_boot,
    ),
)


def host_of(data: dict[str, Any], deck: dict[str, Any]) -> str | None:
    writer = deck.get("writer", "")
    if writer == "ui":
        return "board"
    if writer == "satellite":
        return "companion"
    if writer.startswith("server"):
        return "server"
    for source in data.get("sources", []):
        if source.get("drawing") and source.get("deck") == deck["slot"] and source["kind"] in HOSTS:
            return source["kind"]
    return None


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities(BoardSensor(coordinator, d) for d in BOARD_SENSORS)
    add_per_deck(entry, async_add_entities, lambda c, d: [HostSensor(c, d)])


class BoardSensor(BoardEntity, SensorEntity):
    entity_description: BoardSensorDescription

    def __init__(self, coordinator: DeckCoordinator, description: BoardSensorDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> Any:
        return self.entity_description.value(self.coordinator.data)


class HostSensor(DeckEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.ENUM
    _attr_options = HOSTS

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any]) -> None:
        super().__init__(coordinator, deck, "host")

    @property
    def native_value(self) -> str | None:
        deck = self.deck
        return host_of(self.coordinator.data, deck) if deck else None
