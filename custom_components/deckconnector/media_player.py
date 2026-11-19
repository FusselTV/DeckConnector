from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import Any

from homeassistant.components import media_source
from homeassistant.components.ffmpeg import get_ffmpeg_manager
from homeassistant.components.media_player import MediaPlayerEntity
from homeassistant.components.media_player.browse_media import BrowseMedia, async_process_play_media_url
from homeassistant.components.media_player.const import (
    MediaPlayerDeviceClass,
    MediaPlayerEntityFeature,
    MediaPlayerState,
    MediaType,
)
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import DeckConnectorError
from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry
from .entity import BoardEntity

_LOGGER = logging.getLogger(__name__)

PARALLEL_UPDATES = 1
MAX_S = 600
DECODE_S = 30  # the whole clip is decoded before it plays: a live stream would take MAX_S


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    if entry.runtime_data.data.get("speaker"):
        async_add_entities([Speaker(entry.runtime_data, "speaker")])


async def decode(hass: HomeAssistant, url: str) -> bytes:
    proc = await asyncio.create_subprocess_exec(
        get_ffmpeg_manager(hass).binary,
        *("-nostdin", "-loglevel", "error", "-rw_timeout", "10000000", "-i", url, "-t", str(MAX_S)),
        *("-f", "s16le", "-ac", "1", "-ar", "16000", "-"),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        async with asyncio.timeout(DECODE_S):
            pcm, err = await proc.communicate()
    except TimeoutError:
        pcm, err = b"", f"no end after {DECODE_S} s, a live stream?".encode()
    finally:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()
    if proc.returncode or not pcm:
        raise HomeAssistantError(
            translation_domain=DOMAIN,
            translation_key="cannot_decode",
            translation_placeholders={"error": err.decode(errors="replace").strip()[-300:] or "no audio"},
        )
    return pcm


class Speaker(BoardEntity, MediaPlayerEntity):
    _attr_device_class = MediaPlayerDeviceClass.SPEAKER
    _attr_supported_features = (
        MediaPlayerEntityFeature.PLAY_MEDIA
        | MediaPlayerEntityFeature.STOP
        | MediaPlayerEntityFeature.VOLUME_SET
        | MediaPlayerEntityFeature.MEDIA_ANNOUNCE
        | MediaPlayerEntityFeature.BROWSE_MEDIA
    )
    _playing: asyncio.Task[None] | None = None

    @property
    def state(self) -> MediaPlayerState:
        return MediaPlayerState.PLAYING if self._playing and not self._playing.done() else MediaPlayerState.IDLE

    @property
    def volume_level(self) -> float | None:
        volume = self.coordinator.data.get("volume")
        return None if volume is None else volume / 100

    async def async_set_volume_level(self, volume: float) -> None:
        await self.call("PUT", "/api/config", {"volume": round(volume * 100)})

    async def async_browse_media(
        self, media_content_type: MediaType | str | None = None, media_content_id: str | None = None
    ) -> BrowseMedia:
        return await media_source.async_browse_media(
            self.hass,
            media_content_id,
            # radio stations never end, see DECODE_S
            content_filter=lambda item: (
                item.media_content_type.startswith("audio/")
                and not item.media_content_id.startswith("media-source://radio_browser")
            ),
        )

    async def async_play_media(self, media_type: MediaType | str, media_id: str, **kwargs: Any) -> None:
        if media_source.is_media_source_id(media_id):
            media_id = (await media_source.async_resolve_media(self.hass, media_id, self.entity_id)).url
        pcm = await decode(self.hass, async_process_play_media_url(self.hass, media_id))
        await self.async_media_stop()
        self._playing = self.coordinator.config_entry.async_create_background_task(
            self.hass, self._play(pcm), f"{DOMAIN} speaker"
        )
        # a callback, not the task's own last line: an eager task can end before _playing holds it
        self._playing.add_done_callback(lambda _: self.async_write_ha_state())
        self.async_write_ha_state()

    async def _play(self, pcm: bytes) -> None:
        try:
            if not await self.coordinator.api.play(pcm):
                _LOGGER.warning(
                    "%s ended it early: a sound, another stream or the speaker setting", self.coordinator.data["name"]
                )
        except DeckConnectorError as err:
            _LOGGER.warning("%s did not play it: %s", self.coordinator.data["name"], err)

    async def async_media_stop(self) -> None:
        if task := self._playing:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
