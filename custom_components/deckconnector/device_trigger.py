from __future__ import annotations

import re
from typing import Any

from homeassistant.components.device_automation import DEVICE_TRIGGER_BASE_SCHEMA
from homeassistant.const import CONF_DEVICE_ID, CONF_DOMAIN, CONF_PLATFORM, CONF_TYPE
from homeassistant.core import CALLBACK_TYPE, HassJob, HomeAssistant, callback
from homeassistant.helpers import device_registry as dr, entity_registry as er
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.trigger import TriggerActionType, TriggerInfo
from homeassistant.helpers.typing import ConfigType
import probatio as vol

from .const import DOMAIN, SIGNAL_INPUT
from .event import event_of

CONF_SUBTYPE = "subtype"
TYPES = ("press", "turn_left", "turn_right", "tap", "long_press", "swipe_left", "swipe_right")

TRIGGER_SCHEMA = DEVICE_TRIGGER_BASE_SCHEMA.extend(
    {
        vol.Required(CONF_TYPE): vol.In(TYPES),
        vol.Required(CONF_SUBTYPE): vol.Match(r"^(key_\d+|dial_\d+|touch_strip)$"),
    }
)


def trigger_of(event_type: str) -> tuple[str, str] | None:
    if m := re.fullmatch(r"(key_\d+|dial_\d+)_(press|left|right)", event_type):
        return {"press": "press", "left": "turn_left", "right": "turn_right"}[m[2]], m[1]
    if m := re.fullmatch(r"touch_(tap|long_press|swipe_left|swipe_right)", event_type):
        return m[1], "touch_strip"
    return None


def _serial(hass: HomeAssistant, device_id: str) -> str | None:
    device = dr.async_get(hass).async_get(device_id)
    return next((i for d, i in device.identifiers if d == DOMAIN), None) if device else None


async def async_get_triggers(hass: HomeAssistant, device_id: str) -> list[dict[str, Any]]:
    event_types = [
        event_type
        for e in er.async_entries_for_device(er.async_get(hass), device_id)
        if e.domain == "event" and e.platform == DOMAIN
        for event_type in (e.capabilities or {}).get("event_types", [])
    ]
    return [
        {
            CONF_PLATFORM: "device",
            CONF_DOMAIN: DOMAIN,
            CONF_DEVICE_ID: device_id,
            CONF_TYPE: found[0],
            CONF_SUBTYPE: found[1],
        }
        for event_type in event_types
        if (found := trigger_of(event_type))
    ]


async def async_attach_trigger(
    hass: HomeAssistant, config: ConfigType, action: TriggerActionType, trigger_info: TriggerInfo
) -> CALLBACK_TYPE:
    job = HassJob(action)
    wanted = (config[CONF_TYPE], config[CONF_SUBTYPE])
    serial = _serial(hass, config[CONF_DEVICE_ID])

    @callback
    def on_input(msg: dict[str, Any]) -> None:
        event = event_of(msg)
        if event and trigger_of(event[0]) == wanted:
            hass.async_run_hass_job(
                job,
                {
                    "trigger": {
                        **trigger_info["trigger_data"],
                        CONF_PLATFORM: "device",
                        CONF_DOMAIN: DOMAIN,
                        CONF_DEVICE_ID: config[CONF_DEVICE_ID],
                        CONF_TYPE: wanted[0],
                        CONF_SUBTYPE: wanted[1],
                        "description": f"{wanted[1]} {wanted[0]}",
                        **event[1],
                    }
                },
            )

    return async_dispatcher_connect(hass, SIGNAL_INPUT.format(serial), on_input)
