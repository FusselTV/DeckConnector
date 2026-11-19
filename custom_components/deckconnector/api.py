from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
import contextlib
import json
from typing import Any

import aiohttp

MARK = {"X-DeckConnector": "1"}
TIMEOUT = aiohttp.ClientTimeout(total=15)
PCM_RATE = 16000 * 2  # bytes per second the board's speaker plays


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
    error = body.get("error", f"HTTP {resp.status}") if isinstance(body, dict) else f"HTTP {resp.status}"
    if resp.status == 401:
        raise AuthError(error)
    if resp.status == 409:
        raise ConflictError(error)
    if resp.status == 429:
        raise PausedError(error)
    if resp.status >= 400:
        raise DeckConnectorError(error)
    return body, resp


class Api:
    def __init__(self, session: aiohttp.ClientSession, host: str, token: str) -> None:
        self.session = session
        self.host = host
        self.headers = {"Authorization": f"Bearer {token}"}

    async def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        return (await _call(self.session, method, f"http://{self.host}{path}", json=body, headers=self.headers))[0]

    async def play(self, pcm: bytes) -> bool:
        """Plays 16 kHz mono s16le on the board's speaker; returns when it has played, False when something on
        the board ended it early."""

        async def paced() -> AsyncIterator[bytes]:
            # a second ahead of the speaker: the board buffers three, and what it cannot take yet would wait in
            # its scarce network buffers
            start = asyncio.get_running_loop().time()
            for at in range(0, len(pcm), PCM_RATE // 5):
                ahead = at / PCM_RATE - (asyncio.get_running_loop().time() - start) - 1
                if ahead > 0:
                    await asyncio.sleep(ahead)
                yield pcm[at : at + PCM_RATE // 5]

        # Content-Length set: the board takes no chunked body
        headers = {**self.headers, "Content-Length": str(len(pcm)), "Content-Type": "application/octet-stream"}
        body, _ = await _call(
            self.session,
            "POST",
            f"http://{self.host}/api/sound/stream",
            data=paced(),
            headers=headers,
            timeout=aiohttp.ClientTimeout(total=len(pcm) / PCM_RATE + 30),
        )
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
