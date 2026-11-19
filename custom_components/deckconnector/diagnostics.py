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
    "name",
    "request_name",
    "request",
    "https_fingerprint",
    "https_ca",
    "qr",
    "code",
}


def _secrets(data: Any) -> set[str]:
    if isinstance(data, dict):
        return {v for k, v in data.items() if k in TO_REDACT and isinstance(v, str) and len(v) > 2}.union(
            *(_secrets(v) for v in data.values())
        )
    if isinstance(data, list):
        return set().union(*(_secrets(v) for v in data))
    return set()


def _mask(text: str, secrets: set[str]) -> str:
    for secret in sorted(secrets, key=len, reverse=True):
        text = text.replace(secret, REDACTED)
    return IPV4.sub(REDACTED, text)


async def async_get_config_entry_diagnostics(hass: HomeAssistant, entry: DeckConnectorConfigEntry) -> dict[str, Any]:
    coordinator = entry.runtime_data
    secrets = _secrets(coordinator.data)
    try:
        got = await coordinator.api.request("GET", "/api/events")
        # a page since event codes, a plain list before
        events: Any = [
            {
                **e,
                "text": _mask(str(e.get("text", "")), secrets),
                "args": [_mask(str(a), secrets) for a in e.get("args", [])],
            }
            for e in (got.get("events", []) if isinstance(got, dict) else got)
        ]
    except DeckConnectorError as err:
        events = f"not read: {_mask(str(err), secrets)}"
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
