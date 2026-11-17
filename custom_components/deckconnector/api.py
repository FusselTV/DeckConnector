from __future__ import annotations

import contextlib
from typing import Any

import aiohttp

MARK = {"X-DeckConnector": "1"}
TIMEOUT = aiohttp.ClientTimeout(total=15)


class DeckConnectorError(Exception):
    pass


class AuthError(DeckConnectorError):
    pass


class NoDeckError(DeckConnectorError):
    pass


async def _call(
    session: aiohttp.ClientSession, method: str, url: str, **kwargs: Any
) -> tuple[Any, aiohttp.ClientResponse]:
    try:
        async with session.request(method, url, timeout=TIMEOUT, **kwargs) as resp:
            body = await resp.json(content_type=None)
    except (aiohttp.ClientError, TimeoutError, ValueError) as err:
        raise DeckConnectorError(str(err) or type(err).__name__) from err
    if resp.status == 401:
        raise AuthError((body or {}).get("error", f"HTTP {resp.status}"))
    if resp.status == 409:
        raise NoDeckError((body or {}).get("error", f"HTTP {resp.status}"))
    if resp.status >= 400:
        raise DeckConnectorError((body or {}).get("error", f"HTTP {resp.status}"))
    return body, resp


class Api:
    def __init__(self, session: aiohttp.ClientSession, host: str, token: str) -> None:
        self.session = session
        self.host = host
        self.headers = {"Authorization": f"Bearer {token}"}

    async def request(self, method: str, path: str, body: dict[str, Any] | None = None) -> Any:
        return (await _call(self.session, method, f"http://{self.host}{path}", json=body, headers=self.headers))[0]

    async def status(self) -> dict[str, Any]:
        return await self.request("GET", "/api/status")

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
    body, _ = await _call(
        session, "POST", f"http://{host}/api/login/request", json={"who": "Home Assistant"}, headers=MARK
    )
    return body["id"]


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
    body, _ = await _call(
        session,
        "POST",
        f"http://{host}/api/tokens",
        json={"name": name, "right": "full"},
        headers=headers,
    )
    if cookie:
        with contextlib.suppress(DeckConnectorError):
            await _call(session, "POST", f"http://{host}/api/logout", headers=headers)
    return body["token"]
