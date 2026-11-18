import asyncio
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

import aiohttp
from homeassistant.components.camera import async_get_image
from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.exceptions import HomeAssistantError
import pytest
from test_setup import CHECK, setup

from conftest import HOST, PLUS, XL, Api, status

BOARD = "deckconnector_a1b2c3"
JPG = f"http://{HOST}/api/screen.jpg?deck=A1"


async def test_the_camera_fetches_only_after_a_change(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.get(JPG, content=b"\xff\xd8one")
    assert (await async_get_image(hass, "camera.stream_deck_screen")).content == b"\xff\xd8one"
    assert (await async_get_image(hass, "camera.stream_deck_screen")).content == b"\xff\xd8one"
    assert sum(str(c[1]).startswith(f"http://{HOST}/api/screen.jpg") for c in aioclient_mock.mock_calls) == 1
    aioclient_mock.clear_requests()
    aioclient_mock.get(JPG, status=403)
    await socket.queue.put({"t": "screen", "deck": "A1"})
    await hass.async_block_till_done()
    assert (await async_get_image(hass, "camera.stream_deck_screen")).content == b"\xff\xd8one"
    aioclient_mock.clear_requests()
    aioclient_mock.get(JPG, exc=aiohttp.ClientError)
    assert (await async_get_image(hass, "camera.stream_deck_screen")).content == b"\xff\xd8one"


async def test_every_input_kind_becomes_an_event(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    base = {"t": "input", "deck": "A1", "index": 1, "value": 3, "x": 10, "y": 400, "x2": 300, "y2": 400}
    expected = {
        "key_up": "key_2_release",
        "encoder_down": "dial_2_press",
        "encoder_up": "dial_2_release",
        "encoder_turn": "dial_2_right",
        "touch_tap": "touch_tap",
        "touch_long": "touch_long_press",
        "touch_swipe": "touch_swipe_right",
    }
    for kind, event_type in expected.items():
        await socket.queue.put({**base, "kind": kind})
        await hass.async_block_till_done()
        assert hass.states.get("event.stream_deck_controls").attributes["event_type"] == event_type
    await socket.queue.put({**base, "kind": "unknown"})
    await hass.async_block_till_done()
    assert hass.states.get("event.stream_deck_controls").attributes["event_type"] == "touch_swipe_right"


@pytest.mark.parametrize(
    ("writer", "sources", "host"),
    [
        ("server1", [], "server"),
        ("dock1", [{"kind": "app", "deck": 0, "drawing": True}], "app"),
        ("dock1", [{"kind": "app", "deck": 1, "drawing": True}], "unknown"),
    ],
)
async def test_the_host_sensor(hass, entry, aioclient_mock, socket, writer, sources, host):
    deck = {**PLUS, "writer": writer}
    await setup(hass, entry, aioclient_mock, socket, deck)
    await socket.queue.put({"t": "state", **status(deck), "sources": sources})
    await hass.async_block_till_done()
    assert hass.states.get("sensor.stream_deck_source").state == host


async def test_controller_and_screen_go_to_the_board(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.put(f"http://{HOST}/api/config", json={"ok": True})
    aioclient_mock.post(f"http://{HOST}/api/screen", json={"ok": True})
    await hass.services.async_call(
        "select", "select_option", {"entity_id": f"select.{BOARD}_controller", "option": "app"}, blocking=True
    )
    await hass.services.async_call("switch", "turn_on", {"entity_id": f"switch.{BOARD}_screen"}, blocking=True)
    assert [c[2] for c in aioclient_mock.mock_calls[-2:]] == [{"controller": "app", "host": ""}, {"on": True}]


async def offer(hass, aioclient_mock, check):
    aioclient_mock.clear_requests()
    aioclient_mock.get(CHECK, json=check)
    await hass.data["entity_components"]["update"].get_entity(f"update.{BOARD}_firmware")._fetch_check()


@pytest.mark.usefixtures("entity_registry_enabled_by_default")
async def test_install_loads_the_offered_file(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await offer(hass, aioclient_mock, {"latest": "v1.1.0", "url": "https://example.invalid/fw.dbfw"})
    aioclient_mock.post(f"http://{HOST}/api/ota/url", json={"ok": True})
    await hass.services.async_call("update", "install", {"entity_id": f"update.{BOARD}_firmware"}, blocking=True)
    assert aioclient_mock.mock_calls[-1][2] == {"url": "https://example.invalid/fw.dbfw"}


@pytest.mark.usefixtures("entity_registry_enabled_by_default")
async def test_install_without_an_offer(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await offer(hass, aioclient_mock, {"latest": "v1.1.0"})
    with pytest.raises(HomeAssistantError) as err:
        await hass.services.async_call("update", "install", {"entity_id": f"update.{BOARD}_firmware"}, blocking=True)
    assert err.value.translation_key == "no_update"


async def test_a_failed_poll_without_socket_makes_entities_unavailable(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    await socket.queue.put(None)
    await hass.async_block_till_done()
    aioclient_mock.clear_requests()
    aioclient_mock.get(f"http://{HOST}/api/status", exc=TimeoutError)
    await entry.runtime_data.async_refresh()
    assert not entry.runtime_data.last_update_success
    assert hass.states.get(f"number.{BOARD}_brightness").state == STATE_UNAVAILABLE


class Refused:
    def __init__(self, status: int) -> None:
        self.status = status

    async def __aenter__(self):
        raise aiohttp.WSServerHandshakeError(MagicMock(), (), status=self.status)

    async def __aexit__(self, *exc: object) -> None:
        return None


class Broken:
    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc: object) -> None:
        return None

    async def receive(self, timeout: float):  # noqa: ASYNC109
        raise RuntimeError("bad")


async def test_a_socket_refused_with_401_starts_reauth(hass, entry, aioclient_mock):
    aioclient_mock.get(f"http://{HOST}/api/status", json=status(PLUS))
    aioclient_mock.get(CHECK, json={"latest": None})
    entry.add_to_hass(hass)
    with patch.object(Api, "websocket", return_value=Refused(401)):
        assert await hass.config_entries.async_setup(entry.entry_id)
        await hass.async_block_till_done()
    assert [f["context"]["source"] for f in hass.config_entries.flow.async_progress()] == ["reauth"]


@pytest.mark.parametrize(("socket_", "backoff"), [(Refused(503), [1, 2]), (Broken(), [1, 1])])
async def test_other_socket_failures_retry(hass, entry, aioclient_mock, socket_, backoff):
    aioclient_mock.get(f"http://{HOST}/api/status", json=status(XL))
    aioclient_mock.get(CHECK, json={"latest": None})
    entry.add_to_hass(hass)
    waits: list[float] = []
    stopped = asyncio.Event()

    async def sleep(seconds: float) -> None:
        waits.append(seconds)
        if len(waits) == 2:
            stopped.set()
            await asyncio.Event().wait()

    with (
        patch.object(Api, "websocket", return_value=socket_),
        patch("custom_components.deckconnector.coordinator.asyncio", SimpleNamespace(sleep=sleep)),
    ):
        assert await hass.config_entries.async_setup(entry.entry_id)
        await stopped.wait()
    assert waits == backoff
    assert not hass.config_entries.flow.async_progress()


async def test_api_errors(hass, aioclient_mock):
    from homeassistant.helpers.aiohttp_client import async_get_clientsession

    from custom_components.deckconnector.api import DeckConnectorError

    api = Api(async_get_clientsession(hass), HOST, "k")
    aioclient_mock.get(f"http://{HOST}/api/status", status=502, text="<html>")
    with pytest.raises(DeckConnectorError, match="HTTP 502: not JSON"):
        await api.status()
    aioclient_mock.clear_requests()
    aioclient_mock.get(f"http://{HOST}/api/status", status=500, json=["x"])
    with pytest.raises(DeckConnectorError, match="HTTP 500"):
        await api.status()
