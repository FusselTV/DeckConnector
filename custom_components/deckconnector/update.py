from __future__ import annotations

import contextlib
from datetime import timedelta
from typing import Any

from homeassistant.components.update import UpdateDeviceClass, UpdateEntity, UpdateEntityFeature
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import DeckConnectorError
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity

PARALLEL_UPDATES = 1
SCAN_INTERVAL = timedelta(hours=6)


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([Firmware(entry.runtime_data, "firmware")], update_before_add=True)


class Firmware(BoardEntity, UpdateEntity):
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_supported_features = UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS
    _attr_should_poll = True

    def __init__(self, coordinator: DeckCoordinator, key: str) -> None:
        super().__init__(coordinator, key)
        self._check: dict[str, Any] = {}

    async def async_update(self) -> None:
        with contextlib.suppress(DeckConnectorError):
            self._check = await self.coordinator.api.request("GET", "/api/update/check")

    @property
    def entity_picture(self) -> str | None:
        return None

    @property
    def installed_version(self) -> str | None:
        return self.coordinator.data.get("firmware")

    @property
    def latest_version(self) -> str | None:
        return self._check.get("latest") or self.installed_version

    @property
    def release_url(self) -> str | None:
        return self._check.get("notes")

    @property
    def in_progress(self) -> bool:
        return self.coordinator.ota.get("state") in ("download", "verify", "done")

    @property
    def update_percentage(self) -> int | None:
        return self.coordinator.ota.get("pct") if self.in_progress else None

    async def async_install(self, version: str | None, backup: bool, **kwargs: Any) -> None:
        await self.async_update()
        if not self._check.get("url"):
            raise HomeAssistantError("no newer firmware")
        await self.call("POST", "/api/ota/url", {"url": self._check["url"]})
