import asyncio
import copy
import json
from typing import Any
from unittest.mock import PropertyMock, patch

import aiohttp
import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.deckconnector.api import Api

HOST = "192.0.2.10"
TOKEN = "k" * 64

PLUS = {"model": "Stream Deck +", "serial": "A1", "firmware": "1.0", "keys": 8, "encoders": 4, "writer": "satellite"}
XL = {"model": "Stream Deck XL", "serial": "B2", "firmware": "1.0", "keys": 32, "encoders": 0, "writer": "ui"}

STATUS = {
    "id": "a0b1c2a1b2c3",
    "name": "deckconnector-a1b2c3",
    "firmware": "v1.0.0",
    "controller": "companion",
    "screen_off": False,
    "brightness": 80,
    "volume": 30,
    "screen_off_s": 600,
    "max_decks": 2,
    "speaker": True,
    "decks_over_limit": 0,
    "boot": {"reason": "power", "at": 1794012139},
    "network": {"lan": {"cable": True, "dead": False}, "wlan": {"rssi": -48, "ssid": "studio", "backup": False}},
    "temp_c": 41,
    "sources": [],
}


def status(*decks: dict[str, Any]) -> dict[str, Any]:
    return copy.deepcopy(
        {
            **STATUS,
            "decks": [
                {**d, "slot": i, "number": i + 1, "brightness": 80, "locked": False} for i, d in enumerate(decks)
            ],
        }
    )


class FakeSocket:
    """Stands in for the board's /api/ws: tests put messages, None closes it."""

    def __init__(self) -> None:
        self.queue: asyncio.Queue[dict[str, Any] | None] = asyncio.Queue()
        self.opened = asyncio.Event()

    async def __aenter__(self) -> FakeSocket:
        self.opened.set()
        return self

    async def __aexit__(self, *exc: object) -> None:
        self.opened.clear()

    async def receive(self, timeout: float) -> Any:
        msg = await self.queue.get()
        if msg is None:
            return aiohttp.WSMessage(aiohttp.WSMsgType.CLOSED, None, None)
        return aiohttp.WSMessage(aiohttp.WSMsgType.TEXT, json.dumps(msg), None)


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    return


@pytest.fixture
def entry() -> MockConfigEntry:
    return MockConfigEntry(
        domain="deckconnector",
        unique_id=STATUS["id"],
        title=STATUS["name"],
        data={"host": HOST, "token": TOKEN},
    )


@pytest.fixture
def socket():
    fake = FakeSocket()
    with patch.object(Api, "websocket", return_value=fake):
        yield fake


@pytest.fixture
def entity_registry_enabled_by_default():
    with patch(
        "homeassistant.helpers.entity.Entity.entity_registry_enabled_default",
        new_callable=PropertyMock,
        return_value=True,
    ):
        yield
