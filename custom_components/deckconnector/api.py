from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
import contextlib
import json
import socket
import struct
from typing import Any

import aiohttp
import yarl

MARK = {"X-DeckConnector": "1"}
TIMEOUT = aiohttp.ClientTimeout(total=15)
PCM_RATE = 16000 * 2  # bytes per second the board's speaker plays
ENDLESS = 2**31 - 1  # Content-Length of a stream, 18 h: the board takes no chunked body; 2**32 - 1 reads as -1 there


class DeckConnectorError(Exception):
    pass


class AuthError(DeckConnectorError):
    pass


class ConflictError(DeckConnectorError):
    pass


class NoDeckError(DeckConnectorError):
    pass


class TokenLimitError(DeckConnectorError):
    pass


class HttpsOnlyError(DeckConnectorError):
    pass


class PausedError(DeckConnectorError):
    pass


async def _call(
    session: aiohttp.ClientSession, method: str, url: str, **kwargs: Any
) -> tuple[Any, aiohttp.ClientResponse]:
    kwargs.setdefault("timeout", TIMEOUT)
    try:
        async with session.request(method, url, **kwargs) as resp:
            text = await resp.text()
    except (aiohttp.ClientError, TimeoutError) as err:
        raise DeckConnectorError(str(err) or type(err).__name__) from err
    try:
        body = json.loads(text) if text else None
    except ValueError as err:
        if resp.status == 403 and "HTTPS only" in text:
            raise HttpsOnlyError(text) from err
        raise DeckConnectorError(f"HTTP {resp.status}: not JSON") from err
    _raise_for(resp.status, body)
    return body, resp


def _raise_for(status: int, body: Any) -> None:
    error = body.get("error", f"HTTP {status}") if isinstance(body, dict) else f"HTTP {status}"
    if status == 401:
        raise AuthError(error)
    if status == 409:
        raise ConflictError(error)
    if status == 429:
        raise PausedError(error)
    if status >= 400:
        raise DeckConnectorError(error)


async def _read_all(reader: asyncio.StreamReader) -> bytes:
    got = b""
    with contextlib.suppress(OSError):  # the board resets a connection it stopped reading: keep what came
        while chunk := await reader.read(4096):
            got += chunk
    return got


class Api:
    def __init__(self, session: aiohttp.ClientSession, host: str, token: str) -> None:
        self.session = session
        self.host = host
        self.headers = {"Authorization": f"Bearer {token}"}

    async def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        return (await _call(self.session, method, f"http://{self.host}{path}", json=body, headers=self.headers))[0]

    async def play(self, pcm: AsyncIterator[bytes]) -> bool:
        """Plays 16 kHz mono s16le on the board's speaker as it comes; returns when it has played, False when
        something on the board ended it early. Cancelled, it stops the speaker at once."""
        url = yarl.URL(f"http://{self.host}")
        try:
            async with asyncio.timeout(TIMEOUT.total):
                reader, writer = await asyncio.open_connection(url.host, url.port)
        except (OSError, TimeoutError) as err:
            raise DeckConnectorError(str(err) or type(err).__name__) from err
        answer = asyncio.create_task(_read_all(reader))
        try:
            writer.write(
                f"POST /api/sound/stream HTTP/1.1\r\nHost: {self.host}\r\n"
                f"Authorization: {self.headers['Authorization']}\r\n"
                f"Content-Type: application/octet-stream\r\nContent-Length: {ENDLESS}\r\n\r\n".encode()
            )
            loop = asyncio.get_running_loop()
            start, sent = loop.time(), 0
            with contextlib.suppress(OSError):  # the board ended it: its answer says why
                async for chunk in pcm:
                    # a second ahead of the speaker: the board buffers three, and what it cannot take yet would
                    # wait in its scarce network buffers
                    if (ahead := sent / PCM_RATE - (loop.time() - start) - 1) > 0:
                        await asyncio.sleep(ahead)
                    if answer.done():
                        break
                    writer.write(chunk)
                    await writer.drain()
                    sent += len(chunk)
                else:
                    writer.write_eof()
            async with asyncio.timeout(15):  # the board plays what it holds, at most 3 s, then answers
                got = await answer
        except TimeoutError as err:
            raise DeckConnectorError("no answer after the stream") from err
        finally:
            answer.cancel()
            if not writer.transport.is_closing():
                # a reset, not a close: the board would play the rest of its buffer after a close
                writer.get_extra_info("socket").setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                writer.transport.abort()
        head, _, text = got.partition(b"\r\n\r\n")
        try:
            status, body = int(head.split()[1]), json.loads(text)
        except (IndexError, ValueError) as err:
            raise DeckConnectorError("the board gave no answer") from err
        _raise_for(status, body)
        return not body.get("stopped")

    async def status(self) -> dict[str, Any]:
        body = await self.request("GET", "/api/status")
        if not isinstance(body, dict):
            raise DeckConnectorError("the status is not a JSON object")
        return body

    async def screen_jpg(self, serial: str) -> bytes | None:
        try:
            async with self.session.get(
                f"http://{self.host}/api/screen.jpg",
                params={"deck": serial},
                headers=self.headers,
                timeout=TIMEOUT,
            ) as resp:
                return await resp.read() if resp.status == 200 else None
        except aiohttp.ClientError, TimeoutError:
            return None

    def websocket(self) -> Any:
        return self.session.ws_connect(
            f"http://{self.host}/api/ws", headers=self.headers, timeout=aiohttp.ClientWSTimeout(ws_close=5)
        )


async def login_request(session: aiohttp.ClientSession, host: str) -> str:
    try:
        body, _ = await _call(
            session, "POST", f"http://{host}/api/login/request", json={"who": "Home Assistant"}, headers=MARK
        )
    except ConflictError as err:
        raise NoDeckError(str(err)) from err
    return str(body["id"])


async def login_cancel(session: aiohttp.ClientSession, host: str, request: str) -> None:
    with contextlib.suppress(DeckConnectorError):
        await _call(session, "DELETE", f"http://{host}/api/login/request/{request}", headers=MARK)


async def login_state(session: aiohttp.ClientSession, host: str, request: str) -> tuple[str, str | None]:
    body, resp = await _call(session, "GET", f"http://{host}/api/login/request/{request}")
    cookie = resp.cookies.get("dc_session")
    return body["state"], cookie.value if cookie else None


async def create_token(
    session: aiohttp.ClientSession,
    host: str,
    name: str,
    *,
    cookie: str | None = None,
    password: str | None = None,
) -> str:
    headers = {**MARK, "Cookie": f"dc_session={cookie}"} if cookie else MARK
    if password is not None:
        headers = {**MARK, "Authorization": aiohttp.encode_basic_auth("admin", password)}
    try:
        body, _ = await _call(
            session,
            "POST",
            f"http://{host}/api/tokens",
            json={"name": name, "right": "full"},
            headers=headers,
        )
    except ConflictError as err:
        raise TokenLimitError(str(err)) from err
    if cookie:
        with contextlib.suppress(DeckConnectorError):
            await _call(session, "POST", f"http://{host}/api/logout", headers=headers)
    return str(body["token"])
