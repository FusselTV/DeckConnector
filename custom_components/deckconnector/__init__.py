from __future__ import annotations

import logging

from homeassistant.const import CONF_HOST, CONF_TOKEN, Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr, issue_registry as ir
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import Api
from .const import DOMAIN, ISSUE_DECKS_OVER_LIMIT, ISSUE_HTTPS_ONLY
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator

_LOGGER = logging.getLogger(__name__)

PLATFORMS = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.CAMERA,
    Platform.EVENT,
    Platform.NUMBER,
    Platform.SELECT,
    Platform.SENSOR,
    Platform.SWITCH,
    Platform.UPDATE,
]


async def async_setup_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> bool:
    api = Api(async_get_clientsession(hass), entry.data[CONF_HOST], entry.data[CONF_TOKEN])
    coordinator = DeckCoordinator(hass, entry, api)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    firmware = coordinator.data.get("firmware")

    @callback
    def board_device() -> None:
        data = coordinator.data
        if data.get("firmware") and data["firmware"] != firmware:
            _LOGGER.info("%s runs firmware %s now, loading it again", entry.title, data["firmware"])
            hass.config_entries.async_schedule_reload(entry.entry_id)
            return
        coordinator.board_device_id = (
            dr.async_get(hass)
            .async_get_or_create(
                config_entry_id=entry.entry_id,
                identifiers={(DOMAIN, coordinator.board_id)},
                connections={(dr.CONNECTION_NETWORK_MAC, dr.format_mac(coordinator.board_id))},
                name=data["name"],
                manufacturer="DeckConnector",
                model="DeckConnector",
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
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> None:
    for issue in (ISSUE_HTTPS_ONLY, ISSUE_DECKS_OVER_LIMIT):
        ir.async_delete_issue(hass, DOMAIN, f"{issue}_{entry.entry_id}")


async def async_remove_config_entry_device(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, device: dr.DeviceEntry
) -> bool:
    coordinator = entry.runtime_data
    attached = {coordinator.board_id} | {d["serial"] for d in coordinator.decks}
    return not any(ident in attached for domain, ident in device.identifiers if domain == DOMAIN)
