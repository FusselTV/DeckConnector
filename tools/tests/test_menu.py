"""The web page opens and closes the deck menu, and the status says whether it is open."""

import time

import pytest
from conftest import TARGET, api, status


def wait_for(check, timeout: float = 5) -> dict:
    end = time.time() + timeout
    while True:
        st = status()
        if check(st) or time.time() > end:
            return st
        time.sleep(0.2)


@pytest.mark.sim
def test_the_menu_opens_and_closes_from_the_web():
    if TARGET != "sim" and not status()["device"]:
        pytest.skip("no deck")
    assert status()["menu_open"] is False
    writer = status().get("writer")  # the simulator has none
    assert api("POST", "/api/menu")[0] == 200
    try:
        st = wait_for(lambda s: s["menu_open"])
        assert st["menu_open"] is True and st.get("writer", "ui") == "ui"
    finally:
        assert api("DELETE", "/api/menu")[0] == 200
    st = wait_for(lambda s: not s["menu_open"] and s.get("writer") == writer)
    assert st["menu_open"] is False and st.get("writer") == writer, "the deck did not go back to who drew before"


@pytest.mark.sim
def test_closing_a_closed_menu_is_fine():
    assert api("DELETE", "/api/menu") == (200, b'{"ok":true}')
    assert status()["menu_open"] is False


@pytest.mark.sim
def test_closing_needs_the_login():
    assert api("DELETE", "/api/menu", auth=False)[0] == 401


@pytest.mark.sim
def test_the_password_page_counts_as_open_and_closes_from_the_web():
    if TARGET != "sim" and not status()["device"]:
        pytest.skip("no deck")
    writer = status().get("writer")
    assert api("POST", "/api/password/show")[0] == 200
    try:
        assert wait_for(lambda s: s["menu_open"])["menu_open"] is True
    finally:
        assert api("DELETE", "/api/menu")[0] == 200
    st = wait_for(lambda s: not s["menu_open"] and s.get("writer") == writer)
    assert st["menu_open"] is False and st.get("writer") == writer, "the deck did not go back to who drew before"
