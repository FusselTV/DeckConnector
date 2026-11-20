from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.number import NumberDeviceClass, NumberEntity, NumberEntityDescription
from homeassistant.const import PERCENTAGE, EntityCategory, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class BoardNumberDescription(NumberEntityDescription):
    value: Callable[[dict[str, Any]], float | None]
    payload: Callable[[float], dict[str, int]]
    exists: Callable[[dict[str, Any]], bool] = lambda _: True


NUMBERS = (
    BoardNumberDescription(
        key="brightness",
        native_unit_of_measurement=PERCENTAGE,
        native_min_value=0,
        native_max_value=100,
        native_step=1,
        value=lambda d: d.get("brightness"),
        payload=lambda v: {"brightness": round(v)},
    ),
    BoardNumberDescription(
        key="screen_off_after",
        device_class=NumberDeviceClass.DURATION,
        entity_category=EntityCategory.CONFIG,
        native_unit_of_measurement=UnitOfTime.MINUTES,
        native_min_value=0,
        native_max_value=1440,
        native_step=1,
        value=lambda d: d["screen_off_s"] / 60,
        payload=lambda v: {"screen_off_s": round(v) * 60},
        exists=lambda d: "screen_off_s" in d,
    ),
    BoardNumberDescription(
        key="screen_dim",
        entity_category=EntityCategory.CONFIG,
        native_unit_of_measurement=PERCENTAGE,
        native_min_value=0,
        native_max_value=90,
        native_step=10,
        value=lambda d: d["screen_dim"],
        payload=lambda v: {"screen_dim": round(v / 10) * 10},
        exists=lambda d: "screen_dim" in d,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    async_add_entities(BoardNumber(coordinator, d) for d in NUMBERS if d.exists(coordinator.data))


class BoardNumber(BoardEntity, NumberEntity):
    entity_description: BoardNumberDescription

    def __init__(self, coordinator: DeckCoordinator, description: BoardNumberDescription) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> float | None:
        return self.entity_description.value(self.coordinator.data)

    async def async_set_native_value(self, value: float) -> None:
        await self.call("PUT", "/api/config", self.entity_description.payload(value))
