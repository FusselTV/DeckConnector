from __future__ import annotations

import contextlib
from typing import Any

from homeassistant.components.diagnostics import async_redact_data
from homeassistant.const import CONF_HOST, CONF_TOKEN
from homeassistant.core import HomeAssistant

from .api import DeckConnectorError
from .coordinator import DeckConnectorConfigEntry

TO_REDACT = {
    CONF_TOKEN,
    CONF_HOST,
    "id",
    "ip",
    "host",
    "ssid",
    "bssid",
    "lock",
    "mac",
    "serial",
    "request",
    "https_fingerprint",
    "https_ca",
    "qr",
    "code",
}


async def async_get_config_entry_diagnostics(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> dict[str, Any]:
    coordinator = entry.runtime_data
    events: Any = None
    with contextlib.suppress(DeckConnectorError):
        events = await coordinator.api.request("GET", "/api/events")
    return async_redact_data(
        {
            "entry": dict(entry.data),
            "connected": coordinator.connected,
            "last_update_success": coordinator.last_update_success,
            "status": coordinator.data,
            "ota": coordinator.ota,
            "events": events,
        },
        TO_REDACT,
    )
