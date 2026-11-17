from __future__ import annotations

from homeassistant.const import CONF_HOST, CONF_TOKEN, Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import Api
from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry, DeckCoordinator

PLATFORMS = [
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

    @callback
    def board_device() -> None:
        data = coordinator.data
        coordinator.board_device_id = (
            dr.async_get(hass)
            .async_get_or_create(
                config_entry_id=entry.entry_id,
                identifiers={(DOMAIN, coordinator.board_id)},
                connections={(dr.CONNECTION_NETWORK_MAC, dr.format_mac(coordinator.board_id))},
                name=data["name"],
                manufacturer="DeckConnector",
                model="DeckConnector",
                sw_version=data.get("firmware"),
                configuration_url=f"http://{entry.data[CONF_HOST]}",
            )
            .id
        )

    board_device()
    entry.async_on_unload(coordinator.async_add_listener(board_device))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    coordinator.start()
    return True


async def async_unload_entry(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_config_entry_device(
    hass: HomeAssistant, entry: DeckConnectorConfigEntry, device: dr.DeviceEntry
) -> bool:
    coordinator = entry.runtime_data
    attached = {coordinator.board_id} | {d["serial"] for d in coordinator.decks}
    return not any(ident in attached for domain, ident in device.identifiers if domain == DOMAIN)
