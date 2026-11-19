from __future__ import annotations

import asyncio
from datetime import timedelta
import logging
from typing import Any

import aiohttp
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import Api, AuthError, DeckConnectorError, HttpsOnlyError
from .const import DOCS, DOMAIN, ISSUE_DECKS_OVER_LIMIT, ISSUE_HTTPS_ONLY, SIGNAL_INPUT, SIGNAL_SCREEN

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
        super().__init__(hass, _LOGGER, config_entry=entry, name=DOMAIN, update_interval=POLL)
        self.api = api
        self.connected = False
        self.lost = False
        self.board_device_id = ""
        self.ota: dict[str, Any] = {}

    @property
    def board_id(self) -> str:
        return str(self.data["id"])

    @property
    def decks(self) -> list[dict[str, Any]]:
        return self.data.get("decks") or []

    def deck(self, serial: str) -> dict[str, Any] | None:
        return next((d for d in self.decks if d["serial"] == serial), None)

    def _issue(self, key: str, active: bool, **placeholders: str) -> None:
        issue_id = f"{key}_{self.config_entry.entry_id}"
        if not active:
            ir.async_delete_issue(self.hass, DOMAIN, issue_id)
            return
        ir.async_create_issue(
            self.hass,
            DOMAIN,
            issue_id,
            is_fixable=False,
            severity=ir.IssueSeverity.ERROR if key == ISSUE_HTTPS_ONLY else ir.IssueSeverity.WARNING,
            translation_key=key,
            translation_placeholders={"name": self.config_entry.title, **placeholders},
            learn_more_url=f"{DOCS}home-assistant/#troubleshooting",
        )

    async def _async_update_data(self) -> dict[str, Any]:
        try:
            status = await self.api.status()
        except AuthError as err:
            raise ConfigEntryAuthFailed(translation_domain=DOMAIN, translation_key="auth_failed") from err
        except DeckConnectorError as err:
            https_only = isinstance(err, HttpsOnlyError)
            self._issue(ISSUE_HTTPS_ONLY, https_only)
            if self.connected and self.data:
                _LOGGER.debug("poll of %s failed, the websocket is up: %s", self.api.host, err)
                return self.data
            if https_only:
                raise UpdateFailed(translation_domain=DOMAIN, translation_key="https_only") from err
            raise UpdateFailed(
                translation_domain=DOMAIN,
                translation_key="cannot_connect",
                translation_placeholders={"error": str(err)},
            ) from err
        self._issue(ISSUE_HTTPS_ONLY, False)
        return merge(self.data or {}, status)

    @callback
    def check_decks(self) -> None:
        over = int(self.data.get("decks_over_limit") or 0)
        self._issue(ISSUE_DECKS_OVER_LIMIT, over > 0, count=str(over), max=str(self.data.get("max_decks", "")))

    def start(self) -> None:
        self.config_entry.async_create_background_task(self.hass, self._listen(), f"{DOMAIN} websocket")

    @callback
    def _disconnected(self) -> None:
        if self.connected:
            _LOGGER.info("lost the connection to %s", self.api.host)
            self.connected = False
            self.lost = True
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
                        try:
                            self._handle(msg.json())
                        except Exception:
                            _LOGGER.exception("cannot use a message from %s: %.200s", self.api.host, msg.data)
            except aiohttp.WSServerHandshakeError as err:
                if err.status == 401:
                    self._disconnected()
                    self.config_entry.async_start_reauth(self.hass)
                    return
                _LOGGER.debug("websocket refused: %s", err)
            except (aiohttp.ClientError, TimeoutError) as err:
                _LOGGER.debug("websocket lost: %s", err)
            except Exception:
                _LOGGER.exception("unexpected message from %s", self.api.host)
            self._disconnected()
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, BACKOFF_MAX_S)

    @callback
    def _handle(self, msg: dict[str, Any]) -> None:
        kind = msg.get("t")
        if kind == "state":
            if not self.connected:
                for deck in msg.get("decks") or []:
                    async_dispatcher_send(self.hass, SIGNAL_SCREEN.format(deck["serial"]))
                if self.lost:
                    _LOGGER.info("connected to %s again", self.api.host)
                    self.lost = False
            self.connected = True
            self.async_set_updated_data(merge(self.data, msg))
        elif kind == "input" and not msg.get("simulated"):
            deck = self.deck(msg.get("deck", ""))
            if msg.get("kind") in ("touch_tap", "touch_long") and deck and deck["encoders"] and deck.get("width"):
                # one zone above each dial, as Companion and the web UI split the strip
                zone = msg["x"] * deck["encoders"] // deck["width"]
                msg = {**msg, "zone": min(zone, deck["encoders"] - 1) + 1}
            async_dispatcher_send(self.hass, SIGNAL_INPUT.format(msg.get("deck")), msg)
        elif kind == "screen":
            async_dispatcher_send(self.hass, SIGNAL_SCREEN.format(msg.get("deck")))
        elif kind == "ota":
            self.ota = msg
            self.async_update_listeners()
