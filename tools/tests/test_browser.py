"""The web page in a real browser: login, the list of hosts, a click that reaches the deck, the welcome steps.

The async API, so the test servers on the same event loop keep answering the bridge while the browser works."""

import asyncio
import json

import pytest
from conftest import HOST, PASSWORD, REACT_PAGE, api, status

pytestmark = pytest.mark.skipif(REACT_PAGE, reason="tests the old page; web/e2e tests the React one")

async_api = pytest.importorskip("playwright.async_api")


@pytest.fixture
async def page(servers):
    async with async_api.async_playwright() as p:
        browser = await p.chromium.launch()
        context = await browser.new_context(viewport={"width": 390, "height": 900})
        page = await context.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.goto(f"http://{HOST}/")
        await page.wait_for_selector("#pw", state="visible")
        await page.fill("#pw", "wrong")
        await page.click("#login button")
        await page.wait_for_selector("#loginmsg:has-text('Falsches Passwort')")
        # through the request context, so a failing step never prints the password
        await context.request.post(
            f"http://{HOST}/api/login", data=json.dumps({"password": PASSWORD}), headers={"X-DeckConnector": "1"}
        )
        await page.reload()
        await page.wait_for_selector("#online.ok")
        yield page
        await context.request.post(f"http://{HOST}/api/logout", headers={"X-DeckConnector": "1"})
        assert errors == []
        await browser.close()


async def test_overview_on_a_phone(page):
    first = page.locator("#sources .src").first  # the host that draws comes first, once the servers are in
    await async_api.expect(first.locator(".badge")).to_have_text("zeichnet", timeout=20000)
    assert (await first.locator("b").inner_text()).endswith(" · Server")
    await page.wait_for_selector("#deck .hit")
    assert await page.evaluate("document.documentElement.scrollWidth - document.documentElement.clientWidth") == 0
    await page.click('[data-tab="system"]')
    await page.wait_for_selector("#sys b")
    assert page.url.endswith("#system")


async def test_a_click_reaches_the_deck(page):
    before = (await asyncio.to_thread(status))["counters"]["inputs"]
    await page.wait_for_selector("#deck .hit")
    await page.locator("#deck .hit").first.click()
    await page.locator(".enc [data-turn='1']").first.click()
    await page.wait_for_timeout(500)
    assert (await asyncio.to_thread(status))["counters"]["inputs"] >= before + 3  # down, up, turn


async def test_welcome_steps(page):
    await asyncio.to_thread(api, "PUT", "/api/config", json.dumps({"setup_done": False}).encode())
    try:
        await page.reload()
        await page.wait_for_selector("#wizard[open]")
        await page.click("#wnext")
        await page.click('#wctrl button:has-text("8766")')  # the second test server, so the pick shows
        await page.click("#wnext")
        await page.wait_for_selector("#wtitle:has-text('WLAN')")
        await page.click("#wnext")
        await page.wait_for_selector("#wtitle:has-text('Fertig')")
        assert await page.locator("#wskip").is_hidden()
        await page.click("#wnext")
        await page.wait_for_selector("#wizard[open]", state="detached")
        cfg = json.loads((await asyncio.to_thread(api, "GET", "/api/config"))[1])
        assert cfg["setup_done"] and cfg["controller"] == "server" and cfg["host"].endswith(":8766/deckbridge")
    finally:
        await asyncio.to_thread(api, "PUT", "/api/config", json.dumps({"setup_done": True, "host": ""}).encode())
