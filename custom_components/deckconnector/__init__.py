from __future__ import annotations

import logging

from homeassistant.config_entries import SIGNAL_CONFIG_ENTRY_CHANGED, ConfigEntry, ConfigEntryChange
from homeassistant.const import CONF_HOST, CONF_TOKEN, Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import (
    config_validation as cv,
    device_registry as dr,
    entity_registry as er,
    issue_registry as ir,
)
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.typing import ConfigType

from .api import Api, AuthError, DeckConnectorError
from .const import DOMAIN, ISSUE_DECKS_OVER_LIMIT, ISSUE_HTTPS_ONLY
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator

_LOGGER = logging.getLogger(__name__)

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)

PLATFORMS = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.CAMERA,
    Platform.EVENT,
    Platform.MEDIA_PLAYER,
    Platform.NUMBER,
    Platform.SELECT,
    Platform.SENSOR,
    Platform.SWITCH,
    Platform.UPDATE,
]


@callback
def clear_issues(hass: HomeAssistant, entry_id: str) -> None:
    for issue in (ISSUE_HTTPS_ONLY, ISSUE_DECKS_OVER_LIMIT):
        ir.async_delete_issue(hass, DOMAIN, f"{issue}_{entry_id}")


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    @callback
    def changed(change: ConfigEntryChange, entry: ConfigEntry) -> None:
        if entry.domain == DOMAIN and entry.disabled_by:
            clear_issues(hass, entry.entry_id)

    async_dispatcher_connect(hass, SIGNAL_CONFIG_ENTRY_CHANGED, changed)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> bool:
    api = Api(async_get_clientsession(hass), entry.data[CONF_HOST], entry.data[CONF_TOKEN])
    coordinator = DeckCoordinator(hass, entry, api)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    firmware = coordinator.data.get("firmware")
    speaker = bool(coordinator.data.get("speaker"))
    gone = [(Platform.NUMBER, "volume")]  # older versions had it; the speaker's media player has the volume now
    if not speaker:
        gone += [(Platform.BUTTON, "find"), (Platform.MEDIA_PLAYER, "speaker")]
    ent_reg = er.async_get(hass)
    for platform, key in gone:
        if entity_id := ent_reg.async_get_entity_id(platform, DOMAIN, f"{coordinator.board_id}_{key}"):
            ent_reg.async_remove(entity_id)
    reloading = False

    @callback
    def board_device() -> None:
        nonlocal reloading
        data = coordinator.data
        if reloading:
            return
        if (data.get("firmware") and data["firmware"] != firmware) or bool(data.get("speaker")) != speaker:
            _LOGGER.info(
                "%s runs firmware %s, speaker %s now, loading it again",
                entry.title,
                data.get("firmware"),
                bool(data.get("speaker")),
            )
            reloading = True
            hass.config_entries.async_schedule_reload(entry.entry_id)
            return
        coordinator.board_device_id = (
            dr.async_get(hass)
            .async_get_or_create(
                config_entry_id=entry.entry_id,
                identifiers={(DOMAIN, coordinator.board_id)},
                connections={(dr.CONNECTION_NETWORK_MAC, dr.format_mac(coordinator.board_id))},
                name=data["name"],
                manufacturer="Deck Connector",
                model="Deck Connector",
                sw_version=firmware,
                configuration_url=f"http://{entry.data[CONF_HOST]}",
            )
            .id
        )
        coordinator.check_decks()

    board_device()
    entry.async_on_unload(coordinator.async_add_listener(board_device))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    coordinator.start()
    return True


async def async_unload_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> bool:
    clear_issues(hass, entry.entry_id)
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> None:
    clear_issues(hass, entry.entry_id)
    api = Api(async_get_clientsession(hass), entry.data[CONF_HOST], entry.data[CONF_TOKEN])
    try:
        await api.request("DELETE", "/api/token")
    except AuthError:
        pass  # the key no longer works, so it is gone already
    except DeckConnectorError as err:
        _LOGGER.warning("%s keeps the API key of Home Assistant (%s), delete it under Access", entry.title, err)


async def async_remove_config_entry_device(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, device: dr.DeviceEntry
) -> bool:
    coordinator = entry.runtime_data
    attached = {coordinator.board_id} | {d["serial"] for d in coordinator.decks}
    return not any(ident in attached for domain, ident in device.identifiers if domain == DOMAIN)
