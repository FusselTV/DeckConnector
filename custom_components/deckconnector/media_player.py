from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
import contextlib
import logging
from typing import Any

from homeassistant.components import media_source
from homeassistant.components.ffmpeg import get_ffmpeg_manager
from homeassistant.components.media_player import MediaPlayerEntity
from homeassistant.components.media_player.browse_media import BrowseMedia, async_process_play_media_url
from homeassistant.components.media_player.const import (
    ATTR_MEDIA_ANNOUNCE,
    MediaPlayerDeviceClass,
    MediaPlayerEntityFeature,
    MediaPlayerState,
    MediaType,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import PCM_RATE, DeckConnectorError
from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry
from .entity import BoardEntity

_LOGGER = logging.getLogger(__name__)

PARALLEL_UPDATES = 1
FIRST_S = 30  # until ffmpeg gives its first audio


async def async_setup_entry(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    if entry.runtime_data.data.get("speaker"):
        async_add_entities([Speaker(entry.runtime_data, "speaker")])


async def _tail(stream: asyncio.StreamReader) -> bytes:
    last = b""
    while chunk := await stream.read(4096):
        last = (last + chunk)[-300:]
    return last


async def decode(hass: HomeAssistant, url: str) -> AsyncIterator[bytes]:
    """ffmpeg's 16 kHz mono s16le of `url` as it comes, for as long as it lasts; raises cannot_decode when the
    first audio does not come."""
    # a radio server that drops the connection gets a new one; local files do not know these options
    reconnect = ("-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5")
    proc = await asyncio.create_subprocess_exec(
        get_ffmpeg_manager(hass).binary,
        *("-nostdin", "-loglevel", "error", "-rw_timeout", "10000000"),
        *(reconnect if url.startswith(("http://", "https://")) else ()),
        *("-i", url, "-f", "s16le", "-ac", "1", "-ar", "16000", "-"),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    assert proc.stdout and proc.stderr
    stdout = proc.stdout
    err = asyncio.create_task(_tail(proc.stderr))  # read, or a long stream's warnings fill the pipe and stall it

    async def stop() -> None:
        if proc.returncode is None:
            proc.kill()
        await proc.wait()
        await err

    first = b""
    try:
        with contextlib.suppress(TimeoutError):
            async with asyncio.timeout(FIRST_S):
                first = await stdout.read(PCM_RATE // 5)
    finally:
        if not first:
            await stop()
    if not first:
        raise HomeAssistantError(
            translation_domain=DOMAIN,
            translation_key="cannot_decode",
            translation_placeholders={"error": (await err).decode(errors="replace").strip() or "no audio"},
        )

    async def chunks() -> AsyncIterator[bytes]:
        try:
            yield b""  # taken below: from then on aclose() runs the finally, also when nothing reads on
            yield first
            while chunk := await stdout.read(PCM_RATE // 5):
                yield chunk
            if await proc.wait():
                _LOGGER.warning("ffmpeg stopped: %s", (await err).decode(errors="replace").strip())
        finally:
            await stop()

    gen = chunks()
    await anext(gen)
    return gen


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
    _url: str | None = None

    @property
    def state(self) -> MediaPlayerState:
        return MediaPlayerState.PLAYING if self._playing and not self._playing.done() else MediaPlayerState.IDLE

    @property
    def media_content_id(self) -> str | None:
        # Music Assistant knows its own stream by this
        return self._url if self.state == MediaPlayerState.PLAYING else None

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
            content_filter=lambda item: item.media_content_type.startswith("audio/"),
        )

    async def async_play_media(self, media_type: MediaType | str, media_id: str, **kwargs: Any) -> None:
        url = media_id
        if media_source.is_media_source_id(url):
            url = (await media_source.async_resolve_media(self.hass, url, self.entity_id)).url
        pcm = await decode(self.hass, async_process_play_media_url(self.hass, url))
        # after an announcement what played before goes on: Music Assistant leaves that to the player
        resume = self._url if kwargs.get(ATTR_MEDIA_ANNOUNCE) and self.state == MediaPlayerState.PLAYING else None
        await self.async_media_stop()
        self._url = media_id
        task = self._playing = self.coordinator.config_entry.async_create_background_task(
            self.hass, self._play(pcm), f"{DOMAIN} speaker"
        )
        # a callback, not the task's own last line: an eager task can end before _playing holds it
        task.add_done_callback(lambda _: self._ended(task, resume))
        self.async_write_ha_state()

    @callback
    def _ended(self, task: asyncio.Task[None], resume: str | None) -> None:
        self.async_write_ha_state()
        if resume and self._playing is task and not task.cancelled():
            self.coordinator.config_entry.async_create_background_task(
                self.hass, self._resume(resume), f"{DOMAIN} speaker resume"
            )

    async def _resume(self, media_id: str) -> None:
        try:
            await self.async_play_media(MediaType.MUSIC, media_id)
        except HomeAssistantError as err:
            _LOGGER.warning("%s did not go on after the announcement: %s", self.coordinator.data["name"], err)

    async def _play(self, pcm: AsyncIterator[bytes]) -> None:
        try:
            async with contextlib.aclosing(pcm):  # type: ignore[type-var]
                played = await self.coordinator.api.play(pcm)
            if not played:
                _LOGGER.warning(
                    "%s ended it early: a sound, another stream, the speaker setting or 5 s without audio",
                    self.coordinator.data["name"],
                )
        except DeckConnectorError as err:
            _LOGGER.warning("%s did not play it: %s", self.coordinator.data["name"], err)

    async def async_media_stop(self) -> None:
        if task := self._playing:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
