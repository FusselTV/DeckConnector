from __future__ import annotations

import contextlib
from datetime import timedelta
from typing import Any

from homeassistant.components.update import UpdateDeviceClass, UpdateEntity, UpdateEntityFeature
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.event import async_track_time_interval

from .api import DeckConnectorError
from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import BoardEntity

PARALLEL_UPDATES = 1
CHECK_INTERVAL = timedelta(hours=6)


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([Firmware(entry.runtime_data, "firmware")])


class Firmware(BoardEntity, UpdateEntity):
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_supported_features = UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS

    def __init__(self, coordinator: DeckCoordinator, key: str) -> None:
        super().__init__(coordinator, key)
        self._check: dict[str, Any] = {}

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(async_track_time_interval(self.hass, self._fetch_check, CHECK_INTERVAL))
        self.coordinator.config_entry.async_create_background_task(
            self.hass, self._fetch_check(), f"{DOMAIN} update check"
        )

    async def _fetch_check(self, _now: Any = None) -> None:
        with contextlib.suppress(DeckConnectorError):
            self._check = await self.coordinator.api.request("GET", "/api/update/check")
        self.async_write_ha_state()

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
        await self._fetch_check()
        if not self._check.get("url"):
            raise HomeAssistantError(translation_domain=DOMAIN, translation_key="no_update")
        await self.call("POST", "/api/ota/url", {"url": self._check["url"]})
