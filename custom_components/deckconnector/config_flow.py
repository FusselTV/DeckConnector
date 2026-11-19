from __future__ import annotations

import asyncio
from collections.abc import Mapping
import contextlib
import ipaddress
from typing import Any
from urllib.parse import urlsplit

from homeassistant.config_entries import SOURCE_REAUTH, SOURCE_RECONFIGURE, ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST, CONF_PASSWORD, CONF_TOKEN
from homeassistant.helpers import instance_id
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo
import probatio as vol

from .api import (
    Api,
    AuthError,
    DeckConnectorError,
    HttpsOnlyError,
    NoDeckError,
    TokenLimitError,
    create_token,
    login_cancel,
    login_request,
    login_state,
)
from .const import DOMAIN

ASK_S = 70
POLL_S = 1.0


class NotAccepted(DeckConnectorError):
    pass


class DeckConnectorConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    host: str
    token: str
    name: str = ""
    _pairing: asyncio.Task[str] | None = None
    _error: str | None = None

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            self.host = host_of(user_input[CONF_HOST])
            errors["base"] = "cannot_connect"
            try:
                await Api(async_get_clientsession(self.hass), self.host, "").status()
            except AuthError:
                return await self.async_step_auth()
            except HttpsOnlyError:
                errors["base"] = "https_only"
            except DeckConnectorError:
                pass
            else:
                errors["base"] = "not_deckconnector"
        return self.async_show_form(
            step_id="user",
            data_schema=self.add_suggested_values_to_schema(vol.Schema({vol.Required(CONF_HOST): str}), user_input),
            errors=errors,
        )

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        port = discovery_info.port or 80
        self.host = discovery_info.host if port == 80 else f"{discovery_info.host}:{port}"
        self.name = discovery_info.name.removesuffix(f".{discovery_info.type}")
        if board_id := discovery_info.properties.get("id"):
            await self.async_set_unique_id(board_id)
            self._abort_if_unique_id_configured(updates={CONF_HOST: self.host})
        self.context["title_placeholders"] = {"name": self.name}
        return await self.async_step_auth()

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        self.host = entry_data[CONF_HOST]
        self.name = self._get_reauth_entry().title
        return await self.async_step_auth()

    async def async_step_reconfigure(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reconfigure_entry()
        self.name = entry.title
        errors: dict[str, str] = {}
        if user_input is not None:
            self.host = host_of(user_input[CONF_HOST])
            self.token = entry.data[CONF_TOKEN]
            try:
                await Api(async_get_clientsession(self.hass), self.host, self.token).status()
            except AuthError:
                return await self.async_step_auth()
            except HttpsOnlyError:
                errors["base"] = "https_only"
            except DeckConnectorError:
                errors["base"] = "cannot_connect"
            else:
                return await self.async_step_finish()
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=self.add_suggested_values_to_schema(
                vol.Schema({vol.Required(CONF_HOST): str}), user_input or {CONF_HOST: entry.data[CONF_HOST]}
            ),
            errors=errors,
            description_placeholders={"name": self.name},
        )

    async def async_step_auth(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        return self.async_show_menu(
            step_id="auth",
            menu_options=["pair", "password"],
            description_placeholders={"name": self.name or self.host},
        )

    async def async_step_pair(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if self._pairing is None:
            self._pairing = self.hass.async_create_task(self._pair())
        if not self._pairing.done():
            return self.async_show_progress(
                step_id="pair",
                progress_action="pair",
                progress_task=self._pairing,
                description_placeholders={"name": self.name or self.host},
            )
        task, self._pairing = self._pairing, None
        try:
            self.token = task.result()
        except NoDeckError:
            self._error = "no_deck"
        except TokenLimitError:
            self._error = "too_many_keys"
        except NotAccepted:
            self._error = "not_accepted"
        except DeckConnectorError:
            self._error = "cannot_connect"
        else:
            return self.async_show_progress_done(next_step_id="finish")
        return self.async_show_progress_done(next_step_id="password")

    async def _pair(self) -> str:
        session = async_get_clientsession(self.hass)
        request = await login_request(session, self.host)
        try:
            for _ in range(ASK_S):
                await asyncio.sleep(POLL_S)
                state, cookie = await login_state(session, self.host, request)
                if state == "accepted" and cookie:
                    break
                if state != "pending":
                    raise NotAccepted
            else:
                raise NotAccepted
        except BaseException:
            await login_cancel(session, self.host, request)
            raise
        return await create_token(session, self.host, await self._token_name(), cookie=cookie)

    async def async_step_password(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors = {"base": self._error} if self._error else {}
        self._error = None
        if user_input and CONF_PASSWORD in user_input:
            try:
                self.token = await create_token(
                    async_get_clientsession(self.hass),
                    self.host,
                    await self._token_name(),
                    password=user_input[CONF_PASSWORD],
                )
            except AuthError:
                errors["base"] = "invalid_auth"
            except TokenLimitError:
                errors["base"] = "too_many_keys"
            except DeckConnectorError:
                errors["base"] = "cannot_connect"
            else:
                return await self.async_step_finish()
        return self.async_show_form(
            step_id="password",
            data_schema=vol.Schema({vol.Required(CONF_PASSWORD): str}),
            errors=errors,
            description_placeholders={"name": self.name or self.host},
        )

    async def async_step_finish(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        try:
            status = await Api(async_get_clientsession(self.hass), self.host, self.token).status()
        except DeckConnectorError:
            return self.async_abort(reason="cannot_connect")
        await self.async_set_unique_id(status["id"], raise_on_progress=False)
        data = {CONF_HOST: self.host, CONF_TOKEN: self.token}
        if self.source in (SOURCE_REAUTH, SOURCE_RECONFIGURE):
            self._abort_if_unique_id_mismatch()
            entry = self._get_reauth_entry() if self.source == SOURCE_REAUTH else self._get_reconfigure_entry()
            return self.async_update_reload_and_abort(entry, data_updates=data)
        self._abort_if_unique_id_configured(updates=data)
        return self.async_create_entry(title=status["name"], data=data)

    def async_remove(self) -> None:
        if self._pairing:
            self._pairing.cancel()

    async def _token_name(self) -> str:
        return f"home-assistant-{(await instance_id.async_get(self.hass))[:8]}"


def host_of(text: str) -> str:
    text = text.strip()
    with contextlib.suppress(ValueError):
        return f"[{ipaddress.IPv6Address(text)}]"
    with contextlib.suppress(ValueError):  # an unclosed [
        return urlsplit(text if "://" in text else f"//{text}").netloc.rpartition("@")[2]
    return text
