from __future__ import annotations

import asyncio
from datetime import timedelta
import logging
from typing import Any

import aiohttp
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import Api, AuthError, DeckConnectorError
from .const import DOMAIN, SIGNAL_INPUT, SIGNAL_SCREEN

_LOGGER = logging.getLogger(__name__)

POLL = timedelta(seconds=60)
SILENCE_S = 12
BACKOFF_MAX_S = 30

type DeckConnectorConfigEntry = ConfigEntry[DeckCoordinator]


def merge(old: dict[str, Any], new: dict[str, Any]) -> dict[str, Any]:
    out = dict(old)
    for key, value in new.items():
        out[key] = merge(old[key], value) if isinstance(value, dict) and isinstance(old.get(key), dict) else value
    return out


class DeckCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    config_entry: DeckConnectorConfigEntry

    def __init__(self, hass: HomeAssistant, entry: DeckConnectorConfigEntry, api: Api) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN)
        self.api = api
        self.connected = False
        self.board_device_id = ""
        self.ota: dict[str, Any] = {}

    @property
    def board_id(self) -> str:
        return self.data["id"]

    @property
    def decks(self) -> list[dict[str, Any]]:
        return self.data.get("decks") or []

    def deck(self, serial: str) -> dict[str, Any] | None:
        return next((d for d in self.decks if d["serial"] == serial), None)

    async def _async_update_data(self) -> dict[str, Any]:
        try:
            status = await self.api.status()
        except AuthError as err:
            raise ConfigEntryAuthFailed from err
        except DeckConnectorError as err:
            if self.connected and self.data:
                _LOGGER.debug("poll of %s failed, the websocket is up: %s", self.api.host, err)
                return self.data
            raise UpdateFailed(str(err)) from err
        return merge(self.data or {}, status)

    def start(self) -> None:
        entry = self.config_entry
        entry.async_create_background_task(self.hass, self._listen(), f"{DOMAIN} websocket")
        entry.async_on_unload(async_track_time_interval(self.hass, self._poll, POLL))

    async def _poll(self, _now: Any) -> None:
        await self.async_refresh()

    @callback
    def _set_connected(self, connected: bool) -> None:
        if connected != self.connected:
            self.connected = connected
            self.async_update_listeners()

    async def _listen(self) -> None:
        backoff = 1
        while True:
            try:
                async with self.api.websocket() as ws:
                    backoff = 1
                    self.ota = {}
                    while True:
                        msg = await ws.receive(timeout=SILENCE_S)
                        if msg.type != aiohttp.WSMsgType.TEXT:
                            break
                        self._handle(msg.json())
            except aiohttp.WSServerHandshakeError as err:
                if err.status == 401:
                    self._set_connected(False)
                    self.config_entry.async_start_reauth(self.hass)
                    return
                _LOGGER.debug("websocket refused: %s", err)
            except (aiohttp.ClientError, TimeoutError, ValueError) as err:
                _LOGGER.debug("websocket lost: %s", err)
            except Exception:
                _LOGGER.exception("unexpected message from %s", self.api.host)
            self._set_connected(False)
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, BACKOFF_MAX_S)

    @callback
    def _handle(self, msg: dict[str, Any]) -> None:
        kind = msg.get("t")
        if kind == "state":
            if not self.connected:
                for deck in msg.get("decks") or []:
                    async_dispatcher_send(self.hass, SIGNAL_SCREEN.format(deck["serial"]))
            self.connected = True
            self.async_set_updated_data(merge(self.data, msg))
        elif kind == "input" and not msg.get("simulated"):
            async_dispatcher_send(self.hass, SIGNAL_INPUT.format(msg.get("deck")), msg)
        elif kind == "screen":
            async_dispatcher_send(self.hass, SIGNAL_SCREEN.format(msg.get("deck")))
        elif kind == "ota":
            self.ota = msg
            self.async_update_listeners()
