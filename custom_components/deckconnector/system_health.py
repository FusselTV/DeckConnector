from __future__ import annotations

from typing import Any

from homeassistant.components import system_health
from homeassistant.core import HomeAssistant, callback

from .const import DOMAIN
from .coordinator import DeckConnectorConfigEntry


@callback
def async_register(hass: HomeAssistant, register: system_health.SystemHealthRegistration) -> None:
    register.async_register_info(system_health_info)


async def system_health_info(hass: HomeAssistant) -> dict[str, Any]:
    entries: list[DeckConnectorConfigEntry] = hass.config_entries.async_loaded_entries(DOMAIN)
    coordinators = [e.runtime_data for e in entries]
    return {
        "boards": len(entries),
        "connected": sum(c.connected for c in coordinators),
        "decks": sum(len(c.decks) for c in coordinators),
        "firmware": ", ".join(sorted({str(c.data.get("firmware")) for c in coordinators})) or "-",
    }
