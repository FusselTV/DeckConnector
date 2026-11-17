from dataclasses import replace
from ipaddress import ip_address
from unittest.mock import patch

from homeassistant.config_entries import SOURCE_USER, SOURCE_ZEROCONF
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo
import pytest

from conftest import HOST, STATUS, TOKEN

BASE = f"http://{HOST}"
DISCOVERY = ZeroconfServiceInfo(
    ip_address=ip_address(HOST),
    ip_addresses=[ip_address(HOST)],
    hostname="deckconnector-a1b2c3.local.",
    name="deckconnector-a1b2c3._deckconnector-api._tcp.local.",
    port=80,
    type="_deckconnector-api._tcp.local.",
    properties={"path": "/api", "fw": "v1.0.0", "id": STATUS["id"]},
)


@pytest.fixture(autouse=True)
def no_wait():
    with (
        patch("custom_components.deckconnector.config_flow.POLL_S", 0),
        patch("custom_components.deckconnector.async_setup_entry", return_value=True),
    ):
        yield


def board(aioclient_mock, password_ok=True):
    aioclient_mock.post(
        f"{BASE}/api/tokens",
        json={"name": "home-assistant", "right": "full", "token": TOKEN}
        if password_ok
        else {"ok": False, "error": "login required"},
        status=200 if password_ok else 401,
    )
    aioclient_mock.get(f"{BASE}/api/status", json=STATUS)


async def test_manual_with_password(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", status=401, json={"ok": False})
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    assert result["type"] is FlowResultType.MENU
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "password"})
    aioclient_mock.clear_requests()
    board(aioclient_mock)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"password": "secret"})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == STATUS["name"]
    assert result["data"] == {"host": HOST, "token": TOKEN}
    assert result["result"].unique_id == STATUS["id"]
    name = aioclient_mock.mock_calls[0][2]["name"]
    assert name.startswith("home-assistant-") and len(name) <= 23


async def test_manual_unreachable(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", exc=TimeoutError)
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    assert result["errors"] == {"base": "cannot_connect"}


async def test_wrong_password(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", status=401, json={"ok": False})
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "password"})
    aioclient_mock.clear_requests()
    board(aioclient_mock, password_ok=False)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"password": "wrong"})
    assert result["errors"] == {"base": "invalid_auth"}


async def pair(hass, flow_id):
    result = await hass.config_entries.flow.async_configure(flow_id, {"next_step_id": "pair"})
    if result["type"] is not FlowResultType.SHOW_PROGRESS:
        return result
    await hass.async_block_till_done()
    return await hass.config_entries.flow.async_configure(flow_id)


async def test_zeroconf_and_confirm_on_the_deck(hass, aioclient_mock):
    result = await hass.config_entries.flow.async_init(
        "deckconnector", context={"source": SOURCE_ZEROCONF}, data=DISCOVERY
    )
    assert result["type"] is FlowResultType.MENU
    aioclient_mock.post(f"{BASE}/api/login/request", json={"id": "f" * 32, "s": 60})
    aioclient_mock.get(
        f"{BASE}/api/login/request/{'f' * 32}",
        json={"state": "accepted", "s": 0},
        cookies={"dc_session": "c" * 64},
    )
    aioclient_mock.post(f"{BASE}/api/logout", json={"ok": True})
    board(aioclient_mock)
    result = await pair(hass, result["flow_id"])
    assert result["type"] is FlowResultType.CREATE_ENTRY
    token_call = next(c for c in aioclient_mock.mock_calls if str(c[1]).endswith("/api/tokens"))
    assert token_call[3]["Cookie"] == f"dc_session={'c' * 64}"


async def test_no_deck_falls_back_to_the_password(hass, aioclient_mock):
    result = await hass.config_entries.flow.async_init(
        "deckconnector", context={"source": SOURCE_ZEROCONF}, data=DISCOVERY
    )
    aioclient_mock.post(f"{BASE}/api/login/request", status=409, json={"ok": False, "error": "no deck attached"})
    result = await pair(hass, result["flow_id"])
    assert result["step_id"] == "password" and result["errors"] == {"base": "no_deck"}


async def test_zeroconf_updates_the_host_of_a_known_board(hass, entry):
    entry.add_to_hass(hass)
    moved = replace(DISCOVERY, ip_address=ip_address("192.0.2.99"))
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_ZEROCONF}, data=moved)
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "already_configured"
    assert entry.data["host"] == "192.0.2.99"


async def test_reauth(hass, entry, aioclient_mock):
    entry.add_to_hass(hass)
    result = await entry.start_reauth_flow(hass)
    assert result["type"] is FlowResultType.MENU
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "password"})
    aioclient_mock.post(f"{BASE}/api/tokens", json={"name": "x", "right": "full", "token": "n" * 64})
    aioclient_mock.get(f"{BASE}/api/status", json=STATUS)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"password": "secret"})
    assert result["type"] is FlowResultType.ABORT and result["reason"] == "reauth_successful"
    assert entry.data["token"] == "n" * 64


async def test_https_in_the_host_is_dropped(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", status=401, json={"ok": False})
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": f"https://{HOST}/"})
    assert result["type"] is FlowResultType.MENU


async def test_https_only_board(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", status=403, text="HTTPS only")
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    assert result["errors"] == {"base": "https_only"}


async def test_eight_keys_already(hass, aioclient_mock):
    aioclient_mock.get(f"{BASE}/api/status", status=401, json={"ok": False})
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "password"})
    aioclient_mock.post(f"{BASE}/api/tokens", status=409, json={"ok": False, "error": "8 tokens already"})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"password": "secret"})
    assert result["errors"] == {"base": "too_many_keys"}


async def test_manual_add_while_the_zeroconf_flow_waits(hass, aioclient_mock):
    waiting = await hass.config_entries.flow.async_init(
        "deckconnector", context={"source": SOURCE_ZEROCONF}, data=DISCOVERY
    )
    assert waiting["type"] is FlowResultType.MENU
    aioclient_mock.get(f"{BASE}/api/status", status=401, json={"ok": False})
    result = await hass.config_entries.flow.async_init("deckconnector", context={"source": SOURCE_USER})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"host": HOST})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"next_step_id": "password"})
    aioclient_mock.clear_requests()
    board(aioclient_mock)
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"password": "secret"})
    assert result["type"] is FlowResultType.CREATE_ENTRY


async def test_a_denied_request_is_taken_back(hass, aioclient_mock):
    result = await hass.config_entries.flow.async_init(
        "deckconnector", context={"source": SOURCE_ZEROCONF}, data=DISCOVERY
    )
    request = f"{BASE}/api/login/request/{'f' * 32}"
    aioclient_mock.post(f"{BASE}/api/login/request", json={"id": "f" * 32, "s": 60})
    aioclient_mock.get(request, json={"state": "denied", "s": 0})
    aioclient_mock.delete(request, json={"ok": True})
    result = await pair(hass, result["flow_id"])
    assert result["errors"] == {"base": "not_accepted"}
    assert any(c[0] == "DELETE" and str(c[1]) == request for c in aioclient_mock.mock_calls)
