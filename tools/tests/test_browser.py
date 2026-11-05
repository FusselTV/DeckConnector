"""The web page in a real browser: login, controller cards, a click that reaches the deck, the welcome steps."""

import json

import pytest
from conftest import HOST, PASSWORD, api, status

sync_api = pytest.importorskip("playwright.sync_api")


@pytest.fixture
def page(servers):
    with sync_api.sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 390, "height": 900})
        page = context.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"http://{HOST}/")
        page.wait_for_selector("#pw", state="visible")
        page.fill("#pw", "wrong")
        page.click("#login button")
        page.wait_for_selector("#loginmsg:has-text('Falsches Passwort')")
        # through the request context, so a failing step never prints the password
        context.request.post(f"http://{HOST}/api/login", data=json.dumps({"password": PASSWORD}))
        page.reload()
        page.wait_for_selector("#online.ok")
        yield page
        assert errors == []
        browser.close()


def test_overview_on_a_phone(page):
    names = [t.split("\n")[0] for t in page.locator("#ctrl button").all_inner_texts()]
    assert names == ["Companion", "Elgato App", "Eigener Server"]
    assert page.locator('#ctrl [aria-pressed="true"]').inner_text().startswith("Eigener Server")
    page.wait_for_selector("#deck .hit")
    assert page.evaluate("document.documentElement.scrollWidth - document.documentElement.clientWidth") == 0
    page.click('[data-tab="system"]')
    page.wait_for_selector("#sys b")
    assert page.url.endswith("#system")


def test_a_click_reaches_the_deck(page):
    before = status()["counters"]["inputs"]
    page.wait_for_selector("#deck .hit")
    page.locator("#deck .hit").first.click()
    page.locator(".enc [data-turn='1']").first.click()
    page.wait_for_timeout(500)
    assert status()["counters"]["inputs"] >= before + 3  # down, up, turn


def test_welcome_steps(page):
    api("PUT", "/api/config", json.dumps({"setup_done": False}).encode())
    try:
        page.reload()
        page.wait_for_selector("#wizard[open]")
        page.click("#wnext")
        assert page.locator("#wnext").is_disabled(), "nothing is picked for the user"
        page.click('#wctrl [data-c="server"]')
        page.click("#wnext")
        page.wait_for_selector("#wtitle:has-text('WLAN')")
        page.click("#wnext")
        page.wait_for_selector("#wtitle:has-text('Fertig')")
        assert page.locator("#wskip").is_hidden()
        page.click("#wnext")
        page.wait_for_selector("#wizard[open]", state="detached")
        cfg = json.loads(api("GET", "/api/config")[1])
        assert cfg["setup_done"] and cfg["controller"] == "server"
    finally:
        api("PUT", "/api/config", json.dumps({"setup_done": True}).encode())
