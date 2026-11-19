import asyncio
import math
import os
import struct
import time
from unittest.mock import patch
import wave

import aiohttp
from aiohttp import web
from homeassistant.components.media_player.const import MediaPlayerEntityFeature
from homeassistant.exceptions import HomeAssistantError
import pytest
from test_setup import setup

from conftest import HOST, PLUS, TOKEN
from custom_components.deckconnector.api import Api
from custom_components.deckconnector.media_player import decode

PLAYER = "media_player.deckconnector_a1b2c3_speaker"


async def test_the_board_gets_a_paced_body_with_its_length(socket_enabled):
    """A fake board: the firmware's server takes no chunked body, and HA must not run more than a second ahead."""
    got: dict = {}

    async def stream(request: web.Request) -> web.Response:
        got["headers"] = request.headers
        start = time.monotonic()
        got["body"] = await request.read()
        got["took"] = time.monotonic() - start
        return web.json_response({"ok": True, "stopped": False})

    app = web.Application()
    app.router.add_post("/api/sound/stream", stream)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", 0)
    await site.start()
    port = site._server.sockets[0].getsockname()[1]
    pcm = bytes(range(256)) * 200  # 1.6 s at 16 kHz
    try:
        async with aiohttp.ClientSession() as session:
            assert await Api(session, f"127.0.0.1:{port}", TOKEN).play(pcm)
    finally:
        await runner.cleanup()
    assert got["body"] == pcm
    assert got["headers"]["Content-Length"] == str(len(pcm))
    assert "Transfer-Encoding" not in got["headers"]
    assert got["headers"]["Authorization"] == f"Bearer {TOKEN}"
    assert 0.35 < got["took"] < 1  # the last of 1.6 s goes out at 1.4 s, a second ahead


def tone_wav(path, rate=44100, seconds=0.5):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = (int(8000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(int(rate * seconds)))
        w.writeframes(b"".join(struct.pack("<hh", s, s) for s in frames))


async def test_ffmpeg_turns_any_file_into_the_boards_format(hass, tmp_path):
    assert await hass.async_add_executor_job(lambda: __import__("shutil").which("ffmpeg")), "needs ffmpeg"
    await __import__("homeassistant.setup").setup.async_setup_component(hass, "ffmpeg", {})
    tone_wav(tmp_path / "a.wav")
    pcm = await decode(hass, str(tmp_path / "a.wav"))
    assert abs(len(pcm) - 16000) < 400  # half a second, mono, 16 bit, 16 kHz
    with pytest.raises(HomeAssistantError) as err:
        await decode(hass, str(tmp_path / "missing.mp3"))
    assert err.value.translation_key == "cannot_decode"
    assert "missing.mp3" in err.value.translation_placeholders["error"]
    tone_wav(tmp_path / "empty.wav", seconds=0)
    with pytest.raises(HomeAssistantError, match="cannot_decode"):
        await decode(hass, str(tmp_path / "empty.wav"))
    os.mkfifo(tmp_path / "stalled")  # ffmpeg waits for a writer that never comes, like a stalled server
    with patch("custom_components.deckconnector.media_player.DECODE_S", 0.5), pytest.raises(HomeAssistantError) as err:
        await decode(hass, str(tmp_path / "stalled"))
    assert "a live stream?" in err.value.translation_placeholders["error"]


async def test_the_speaker_plays_stops_and_sets_the_volume(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    state = hass.states.get(PLAYER)
    assert state.state == "idle"
    assert state.attributes["volume_level"] == 0.3
    features = state.attributes["supported_features"]
    assert features & MediaPlayerEntityFeature.MEDIA_ANNOUNCE and features & MediaPlayerEntityFeature.BROWSE_MEDIA

    aioclient_mock.put(f"http://{HOST}/api/config", json={"ok": True})
    await hass.services.async_call(
        "media_player", "volume_set", {"entity_id": PLAYER, "volume_level": 0.55}, blocking=True
    )
    assert aioclient_mock.mock_calls[-1][2] == {"volume": 55}

    playing = asyncio.Event()
    stopped = asyncio.Event()

    async def play(self, pcm):
        assert pcm == b"pcm"
        playing.set()
        try:
            await asyncio.sleep(60)
        except asyncio.CancelledError:
            stopped.set()
            raise

    with (
        patch("custom_components.deckconnector.media_player.decode", return_value=b"pcm") as dec,
        patch.object(Api, "play", play),
    ):
        await hass.services.async_call(
            "media_player",
            "play_media",
            {"entity_id": PLAYER, "media_content_type": "music", "media_content_id": "http://x/a.mp3"},
            blocking=True,
        )
        await playing.wait()
        assert dec.call_args.args[1] == "http://x/a.mp3"
        assert hass.states.get(PLAYER).state == "playing"
        await hass.services.async_call("media_player", "media_stop", {"entity_id": PLAYER}, blocking=True)
        assert stopped.is_set()
        await hass.async_block_till_done()
        assert hass.states.get(PLAYER).state == "idle"


async def test_a_board_that_refuses_leaves_the_speaker_idle(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.post(f"http://{HOST}/api/sound/stream", status=409, json={"error": "speaker off"})
    with patch("custom_components.deckconnector.media_player.decode", return_value=b"\0\0" * 10):
        await hass.services.async_call(
            "media_player",
            "play_media",
            {"entity_id": PLAYER, "media_content_type": "music", "media_content_id": "http://x/a.mp3"},
            blocking=True,
        )
        await hass.async_block_till_done()
    assert hass.states.get(PLAYER).state == "idle"
    assert "did not play it: speaker off" in caplog.text


async def test_tts_and_media_source_resolve_before_decoding(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    resolved = type("Play", (), {"url": "/api/tts_proxy/x.mp3"})()
    with (
        patch("homeassistant.components.media_source.async_resolve_media", return_value=resolved),
        patch("custom_components.deckconnector.media_player.decode", return_value=b"\0\0") as dec,
        patch.object(Api, "play"),
    ):
        await hass.services.async_call(
            "media_player",
            "play_media",
            {
                "entity_id": PLAYER,
                "media_content_type": "music",
                "media_content_id": "media-source://tts/x",
                "announce": True,
            },
            blocking=True,
        )
    assert dec.call_args.args[1].endswith("/api/tts_proxy/x.mp3")
    assert dec.call_args.args[1].startswith("http")


async def test_a_stream_the_board_ends_early_is_logged(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    aioclient_mock.post(f"http://{HOST}/api/sound/stream", json={"ok": True, "stopped": True})
    with patch("custom_components.deckconnector.media_player.decode", return_value=b"\0\0" * 10):
        await hass.services.async_call(
            "media_player",
            "play_media",
            {"entity_id": PLAYER, "media_content_type": "music", "media_content_id": "http://x/a.mp3"},
            blocking=True,
        )
        await hass.async_block_till_done()
    assert hass.states.get(PLAYER).state == "idle"
    assert "ended it early" in caplog.text
