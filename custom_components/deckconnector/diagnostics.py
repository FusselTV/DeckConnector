from __future__ import annotations

import re
from typing import Any

from homeassistant.components.diagnostics import REDACTED, async_redact_data
from homeassistant.const import CONF_HOST, CONF_TOKEN
from homeassistant.core import HomeAssistant

from .api import DeckConnectorError
from .coordinator import DeckConnectorConfigEntry

IPV4 = re.compile(r"\b\d{1,3}(?:\.\d{1,3}){3}\b")
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
    try:
        events: Any = [
            {**e, "text": IPV4.sub(REDACTED, str(e.get("text", "")))}
            for e in await coordinator.api.request("GET", "/api/events")
        ]
    except DeckConnectorError as err:
        events = f"not read: {err}"
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
