import asyncio
from collections.abc import AsyncIterator
import io
import json
import math
import os
import socket
import struct
import time
from unittest.mock import patch
import wave

import aiohttp
from aiohttp import web
from homeassistant.components.media_player.const import MediaPlayerEntityFeature
from homeassistant.core import callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_track_state_change_event
import pytest
from test_setup import setup

from conftest import HOST, PLUS, TOKEN
from custom_components.deckconnector.api import Api, ConflictError, DeckConnectorError
from custom_components.deckconnector.media_player import decode

PLAYER = "media_player.deckconnector_a1b2c3_speaker"


class FakeBoard:
    """The firmware's side of POST /api/sound/stream on a plain socket: reads the body until the sender shuts its
    side (or `take` bytes, then answers stopped), answers and closes."""

    def __init__(
        self, take: int | None = None, status: int = 200, answer: dict | None = None, close: str = "close"
    ) -> None:
        self.take, self.status, self.answer, self.close = take, status, answer, close
        self.head = b""
        self.body = bytearray()
        self.ended = ""  # "eof", "reset" or "took"
        self.took = 0.0
        self.done = asyncio.Event()

    async def serve(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.head = await reader.readuntil(b"\r\n\r\n")
        start = time.monotonic()
        stopped = False
        try:
            if self.status == 200:
                while chunk := await reader.read(4096):
                    self.body += chunk
                    if self.take is not None and len(self.body) >= self.take:
                        stopped, self.ended = True, "took"
                        break
                else:
                    self.ended = "eof"
            if self.close == "reset":  # gone without an answer
                writer.get_extra_info("socket").setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                writer.transport.abort()
                return
            answer = self.answer or {"ok": True, "stopped": stopped}
            text = json.dumps(answer).encode()
            writer.write(f"HTTP/1.1 {self.status} X\r\nContent-Length: {len(text)}\r\n\r\n".encode() + text)
            await writer.drain()
            if self.close == "late":  # answered, keeps the connection until the client goes
                while await reader.read(4096):
                    pass
        except ConnectionResetError:
            self.ended = "reset"
        finally:
            self.took = time.monotonic() - start
            writer.close()
            self.done.set()

    async def __aenter__(self) -> str:
        self.server = await asyncio.start_server(self.serve, "127.0.0.1", 0)
        return f"127.0.0.1:{self.server.sockets[0].getsockname()[1]}"

    async def __aexit__(self, *exc) -> None:
        self.server.close()


async def pieces(pcm: bytes, size: int = 6400, pause_at: int = -1, pause: float = 0) -> AsyncIterator[bytes]:
    for at in range(0, len(pcm), size):
        if at == pause_at:
            await asyncio.sleep(pause)
        yield pcm[at : at + size]


async def endless() -> AsyncIterator[bytes]:
    while True:
        yield b"\1\0" * 3200
        await asyncio.sleep(0)


async def test_a_clip_goes_out_paced_and_ends_with_a_half_close(socket_enabled):
    pcm = bytes(range(256)) * 200  # 1.6 s at 16 kHz
    board = FakeBoard()
    async with board as host, aiohttp.ClientSession() as session:
        assert await Api(session, host, TOKEN).play(pieces(pcm))
    assert bytes(board.body) == pcm
    assert board.ended == "eof"
    assert b"Content-Length: 2147483647\r\n" in board.head  # the board takes no chunked body
    assert f"Authorization: Bearer {TOKEN}".encode() in board.head
    assert 0.35 < board.took < 1  # the last of 1.6 s goes out at 1.4 s, a second ahead


async def test_a_pause_in_the_source_keeps_the_stream(socket_enabled):
    pcm = b"\2\0" * 16000
    board = FakeBoard()
    async with board as host, aiohttp.ClientSession() as session:
        assert await Api(session, host, TOKEN).play(pieces(pcm, pause_at=12800, pause=1.5))
    assert bytes(board.body) == pcm


async def test_stop_resets_an_endless_stream_at_once(socket_enabled):
    """A close would let the board play out its buffer; a reset ends it."""
    board = FakeBoard()
    async with board as host, aiohttp.ClientSession() as session:
        task = asyncio.create_task(Api(session, host, TOKEN).play(endless()))
        await asyncio.sleep(0.5)
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        async with asyncio.timeout(1):
            await board.done.wait()
    assert board.ended == "reset"
    assert 30000 < len(board.body) < 60000  # paced: half a second plus the second ahead


async def test_the_board_ending_an_endless_stream_ends_the_upload(socket_enabled):
    board = FakeBoard(take=20000)
    async with board as host, aiohttp.ClientSession() as session:
        async with asyncio.timeout(3):
            assert not await Api(session, host, TOKEN).play(endless())


async def test_a_refusal_raises(socket_enabled):
    board = FakeBoard(status=409, answer={"error": "speaker off"})
    async with board as host, aiohttp.ClientSession() as session:
        with pytest.raises(ConflictError, match="speaker off"):
            await Api(session, host, TOKEN).play(endless())


def tone_wav(path, rate=44100, seconds=0.5):
    with wave.open(path if isinstance(path, io.BytesIO) else str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = (int(8000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(int(rate * seconds)))
        w.writeframes(b"".join(struct.pack("<hh", s, s) for s in frames))


async def test_an_answer_counts_before_the_board_closes(socket_enabled):
    """A refusal the board answers without closing (httpd would read the endless body) must not hold HA."""
    board = FakeBoard(take=20000, close="late")
    async with board as host, aiohttp.ClientSession() as session:
        async with asyncio.timeout(3):
            assert not await Api(session, host, TOKEN).play(endless())


async def test_the_board_answers_while_the_source_is_silent(socket_enabled):
    """A radio that stalls: the board ends the stream after 5 s and HA must see it without a next chunk."""

    async def stalls() -> AsyncIterator[bytes]:
        yield b"\1\0" * 3200
        await asyncio.sleep(60)

    board = FakeBoard(take=6400)
    async with board as host, aiohttp.ClientSession() as session:
        async with asyncio.timeout(3):
            assert not await Api(session, host, TOKEN).play(stalls())


async def test_a_board_gone_without_an_answer_raises(socket_enabled):
    board = FakeBoard(take=20000, close="reset")
    async with board as host, aiohttp.ClientSession() as session:
        with pytest.raises(DeckConnectorError, match="no answer"):
            await Api(session, host, TOKEN).play(endless())


async def collect(pcm: AsyncIterator[bytes]) -> bytes:
    return b"".join([chunk async for chunk in pcm])


@pytest.fixture
async def ffmpeg(hass):
    assert await hass.async_add_executor_job(lambda: __import__("shutil").which("ffmpeg")), "needs ffmpeg"
    await __import__("homeassistant.setup").setup.async_setup_component(hass, "ffmpeg", {})


async def test_ffmpeg_turns_any_file_into_the_boards_format(hass, ffmpeg, tmp_path):
    tone_wav(tmp_path / "a.wav")
    pcm = await collect(await decode(hass, str(tmp_path / "a.wav")))
    assert abs(len(pcm) - 16000) < 400  # half a second, mono, 16 bit, 16 kHz
    with pytest.raises(HomeAssistantError) as err:
        await decode(hass, str(tmp_path / "missing.mp3"))
    assert err.value.translation_key == "cannot_decode"
    assert "missing.mp3" in err.value.translation_placeholders["error"]
    tone_wav(tmp_path / "empty.wav", seconds=0)
    with pytest.raises(HomeAssistantError, match="cannot_decode"):
        await decode(hass, str(tmp_path / "empty.wav"))
    os.mkfifo(tmp_path / "stalled")  # ffmpeg waits for a writer that never comes, like a stalled server
    with patch("custom_components.deckconnector.media_player.FIRST_S", 0.5), pytest.raises(HomeAssistantError) as err:
        await decode(hass, str(tmp_path / "stalled"))
    assert err.value.translation_placeholders["error"] == "no audio"


@pytest.fixture
async def radio(socket_enabled):
    """A radio station over HTTP that never ends; `gone` is set when its listener leaves."""
    gone = asyncio.Event()

    async def live(request: web.Request) -> web.StreamResponse:
        resp = web.StreamResponse(headers={"Content-Type": "audio/wav"})
        await resp.prepare(request)
        head = io.BytesIO()
        tone_wav(head, seconds=0)
        header = bytearray(head.getvalue())
        header[4:8] = header[40:44] = b"\xff\xff\xff\xff"  # no end
        try:
            await resp.write(bytes(header))
            while True:
                await resp.write(b"\0\1" * 4410)  # 50 ms of 44.1 kHz stereo
                await asyncio.sleep(0.05)
        except ConnectionResetError, asyncio.CancelledError:
            gone.set()
            raise

    app = web.Application()
    app.router.add_get("/live.wav", live)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", 0)
    await site.start()
    yield f"http://127.0.0.1:{site._server.sockets[0].getsockname()[1]}/live.wav", gone
    await runner.cleanup()


async def test_a_live_stream_flows_until_it_is_closed(hass, ffmpeg, radio):
    url, gone = radio
    start = time.monotonic()
    pcm = await decode(hass, url)
    got = 0
    async for chunk in pcm:
        got += len(chunk)
        if got >= 32000 * 2:
            break
    assert 1.5 < time.monotonic() - start < 10  # as it comes (after ffmpeg's probing), not at an end
    await pcm.aclose()
    async with asyncio.timeout(5):
        await gone.wait()


async def test_a_refused_play_ends_ffmpeg(hass, entry, aioclient_mock, socket, ffmpeg, radio, caplog):
    """decode's ffmpeg dies with the play that never read from it."""
    url, gone = radio
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    with patch.object(Api, "play", side_effect=ConflictError("speaker off")):
        await play_media(hass, url)
        async with asyncio.timeout(5):
            await gone.wait()
    assert "did not play it: speaker off" in caplog.text


def source(*chunks: bytes, then_wait: bool = False):
    """A stand-in for decode: its chunks, then the end or (then_wait) nothing for a minute; closed records it."""
    closed = asyncio.Event()

    async def gen() -> AsyncIterator[bytes]:
        try:
            yield b""  # primed like decode's
            for chunk in chunks:
                yield chunk
            if then_wait:
                await asyncio.sleep(60)
        finally:
            closed.set()

    async def fake_decode(hass, url):
        g = gen()
        await anext(g)
        return g

    return fake_decode, closed


async def play_media(hass, media_id: str, **data) -> None:
    await hass.services.async_call(
        "media_player",
        "play_media",
        {"entity_id": PLAYER, "media_content_type": "music", "media_content_id": media_id, **data},
        blocking=True,
    )


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
        assert [chunk async for chunk in pcm] == [b"pcm"]
        playing.set()
        try:
            await asyncio.sleep(60)
        except asyncio.CancelledError:
            stopped.set()
            raise

    fake_decode, _closed = source(b"pcm")
    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode) as dec,
        patch.object(Api, "play", play),
    ):
        await play_media(hass, "http://x/a.mp3")
        await playing.wait()
        assert dec.call_args.args[1] == "http://x/a.mp3"
        assert hass.states.get(PLAYER).state == "playing"
        await hass.services.async_call("media_player", "media_stop", {"entity_id": PLAYER}, blocking=True)
        assert stopped.is_set()
        await hass.async_block_till_done()
        assert hass.states.get(PLAYER).state == "idle"


async def test_stop_ends_an_endless_source(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    reading = asyncio.Event()

    async def play(self, pcm):
        async for _chunk in pcm:
            reading.set()
        return True

    fake_decode, closed = source(b"\0\0", then_wait=True)
    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode),
        patch.object(Api, "play", play),
    ):
        await play_media(hass, "http://radio/live.mp3")
        await reading.wait()
        assert hass.states.get(PLAYER).state == "playing"
        await hass.services.async_call("media_player", "media_stop", {"entity_id": PLAYER}, blocking=True)
        assert closed.is_set(), "ffmpeg is closed with the stream"
        assert hass.states.get(PLAYER).state == "idle"


async def test_music_assistant_sees_its_stream_and_the_end_of_it(hass, entry, aioclient_mock, socket):
    """Music Assistant plays its queue as one stream (flow mode), knows it by media_content_id, and takes the
    state going idle as the end."""
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    url = "http://192.0.2.5:8097/flow/session/speaker/abc.flac"
    release = asyncio.Event()

    async def play(self, pcm):
        await collect(pcm)
        await release.wait()
        return True

    fake_decode, _closed = source(b"\0\0" * 100)
    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode) as dec,
        patch.object(Api, "play", play),
    ):
        await play_media(hass, url, enqueue="replace", extra={"metadata": {"title": "Song", "metadataType": 3}})
        assert dec.call_args.args[1] == url
        state = hass.states.get(PLAYER)
        assert (state.state, state.attributes["media_content_id"]) == ("playing", url)
        idle = asyncio.Event()
        async_track_state_change_event(
            hass, PLAYER, callback(lambda e: e.data["new_state"].state == "idle" and idle.set())
        )
        release.set()
        async with asyncio.timeout(2):
            await idle.wait()
        assert "media_content_id" not in hass.states.get(PLAYER).attributes


async def test_an_announcement_goes_back_to_what_played(hass, entry, aioclient_mock, socket):
    """Music Assistant announces with announce=True and leaves going back to its stream to the player."""
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    radio_url = "http://192.0.2.5:8097/flow/session/speaker/abc.flac"
    started: list[str] = []
    resumed = asyncio.Event()

    async def fake_decode(hass, url):
        started.append(url)
        if len(started) == 3:
            resumed.set()
        return await source(b"\0\0", then_wait=url == radio_url)[0](hass, url)

    async def play(self, pcm):
        await collect(pcm)
        return True

    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode),
        patch.object(Api, "play", play),
    ):
        await play_media(hass, radio_url)
        await play_media(hass, "http://x/ding.mp3", announce=True)
        async with asyncio.timeout(2):
            await resumed.wait()
        await hass.async_block_till_done()
        assert started == [radio_url, "http://x/ding.mp3", radio_url]
        assert hass.states.get(PLAYER).attributes["media_content_id"] == radio_url
        await hass.services.async_call("media_player", "media_stop", {"entity_id": PLAYER}, blocking=True)


async def test_a_board_that_refuses_leaves_the_speaker_idle(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    fake_decode, closed = source(b"\0\0" * 10)
    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode),
        patch.object(Api, "play", side_effect=ConflictError("speaker off")),
    ):
        await play_media(hass, "http://x/a.mp3")
        await hass.async_block_till_done()
    assert hass.states.get(PLAYER).state == "idle"
    assert "did not play it: speaker off" in caplog.text
    assert closed.is_set()


async def test_tts_and_media_source_resolve_before_decoding(hass, entry, aioclient_mock, socket):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    resolved = type("Play", (), {"url": "/api/tts_proxy/x.mp3"})()
    fake_decode, _closed = source(b"\0\0")
    with (
        patch("homeassistant.components.media_source.async_resolve_media", return_value=resolved),
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode) as dec,
        patch.object(Api, "play"),
    ):
        await play_media(hass, "media-source://tts/x", announce=True)
    assert dec.call_args.args[1].endswith("/api/tts_proxy/x.mp3")
    assert dec.call_args.args[1].startswith("http")


async def test_a_stream_the_board_ends_early_is_logged(hass, entry, aioclient_mock, socket, caplog):
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    fake_decode, _closed = source(b"\0\0" * 10)
    with (
        patch("custom_components.deckconnector.media_player.decode", side_effect=fake_decode),
        patch.object(Api, "play", return_value=False),
    ):
        await play_media(hass, "http://x/a.mp3")
        await hass.async_block_till_done()
    assert hass.states.get(PLAYER).state == "idle"
    assert "ended it early" in caplog.text
