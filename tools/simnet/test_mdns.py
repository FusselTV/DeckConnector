"""What the board announces over mDNS, from the simulators running its responder (firmware/vendor/mdns)."""

import time

import pytest
from conftest import Seen, browse
from zeroconf import ServiceBrowser, Zeroconf

API = "_deckconnector-api._tcp.local."
ELG = "_elg._tcp.local."


def test_the_bridge_announces_its_api_and_companion_names(sim, zc):
    sim("dca")
    api = browse(zc, API, lambda n: n)
    [info] = api.values()
    assert info.server == "deckconnector-5678ab.local."
    assert info.parsed_addresses() == ["10.77.0.1"]
    assert info.port == 80
    assert info.properties[b"path"] == b"/api"
    assert info.properties[b"id"] == b"0212345678ab"
    assert info.properties[b"fw"]
    for kind in ("_companion-satellite._tcp.local.", "_comp-sat-app._tcp.local."):
        [sat] = browse(zc, kind, lambda n: n).values()
        assert sat.server == info.server and sat.properties == {b"restEnabled": b"true"}


def conflict(sim) -> dict[str, list[str]]:
    """Two bridges that start with the same name, the second one later. What a fresh browser sees afterwards:
    host name -> addresses of the API instances."""
    sim("dca")
    first = Zeroconf(interfaces=["10.77.0.2"])
    browse(first, API, lambda n: n)
    sim("dcb")
    browse(first, API, lambda n: len(n) == 2)
    first.close()
    fresh = Zeroconf(interfaces=["10.77.0.2"])  # the first one still holds the old address under the old name
    try:
        api = browse(fresh, API, lambda n: len(n) == 2)
        return {info.server: info.parsed_addresses() for info in api.values()}
    finally:
        fresh.close()


def test_two_bridges_of_the_same_name_end_up_with_two_names(sim):
    hosts = conflict(sim)
    assert set(hosts) == {"deckconnector-5678ab.local.", "deckconnector-5678ab-2.local."}, hosts
    assert sorted(a for addrs in hosts.values() for a in addrs) == ["10.77.0.1", "10.77.0.2"], hosts


@pytest.mark.xfail(
    strict=True,
    reason="esp mdns applies the probe tiebreak while not probing: the higher address keeps "
    "the name, so the bridge already there renames (RFC 6762 9 wants the newcomer to)",
)
def test_the_newcomer_renames(sim):
    assert conflict(sim)["deckconnector-5678ab.local."] == ["10.77.0.1"]


def test_a_deck_brings_its_dock_and_device(simdeck, zc):
    """The Elgato App finds the dock (dt 215) and the deck behind it (dt 213); both say goodbye when the deck goes."""
    deck = simdeck("plus")
    serial = deck.serial(0)
    elg = browse(zc, ELG, lambda n: len(n) == 2)
    by_type = {info.properties[b"dt"]: info for info in elg.values()}
    dock, device = by_type[b"215"], by_type[b"213"]
    assert dock.name.startswith("Stream Deck Network Dock ")
    assert dock.properties[b"sn"].startswith(b"DB") and dock.port == 5343
    assert device.name == f"{serial}.{ELG}" and device.port == 5344
    assert device.properties[b"md"].startswith(b"Stream Deck +")
    assert {dock.server, device.server} == {"deckconnector-5678ab.local."}

    seen = Seen()
    browser = ServiceBrowser(zc, ELG, seen)
    try:
        assert deck.wait(lambda: seen.names == set(elg)), seen.names
        # an unplug within about a second of a browser's first query left both in its list: open, see the PR
        time.sleep(1.5)
        deck.act("0 unplug")
        assert deck.wait(lambda: not seen.names), f"no goodbye for {seen.names}"
    finally:
        browser.cancel()
