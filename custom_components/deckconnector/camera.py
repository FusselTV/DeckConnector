from __future__ import annotations

from typing import Any

from aiohttp import web
from homeassistant.components.camera import Camera, async_get_still_stream
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import SIGNAL_SCREEN
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator
from .entity import DeckEntity, add_per_deck

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    add_per_deck(entry, async_add_entities, lambda c, d: [ScreenCamera(c, d)] if d.get("width", 1) else [])


class ScreenCamera(DeckEntity, Camera):
    _attr_frame_interval = 0.5

    def __init__(self, coordinator: DeckCoordinator, deck: dict[str, Any]) -> None:
        DeckEntity.__init__(self, coordinator, deck, "screen")
        Camera.__init__(self)
        self._jpg: bytes | None = None
        self._changed = True

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(async_dispatcher_connect(self.hass, SIGNAL_SCREEN.format(self.serial), self._screen))

    @callback
    def _screen(self) -> None:
        self._changed = True

    async def async_camera_image(self, width: int | None = None, height: int | None = None) -> bytes | None:
        if self._changed or self._jpg is None:
            self._changed = False
            jpg = await self.coordinator.api.screen_jpg(self.serial)
            if jpg is None:
                self._changed = True
            else:
                self._jpg = jpg
        return self._jpg

    async def handle_async_mjpeg_stream(self, request: web.Request) -> web.StreamResponse:
        seen: bytes | None = None

        # Chrome shows an MJPEG frame only when the next one arrives, and HA sends a frame only when the bytes change.
        # A copy with a byte after the JPEG end marker follows each new picture, so it shows within one interval.
        async def frame() -> bytes | None:
            nonlocal seen
            jpg = await self.async_camera_image()
            if jpg is None or jpg != seen:
                seen = jpg
                return jpg
            return jpg + b"\0"

        return await async_get_still_stream(request, frame, self.content_type, self.frame_interval)
