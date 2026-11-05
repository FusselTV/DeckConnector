"""The deck picture in the web page must be what the deck shows, also after the bridge restarted."""

import asyncio
import io
import json

import pytest
from conftest import HOST, PASSWORD, api, jpeg
from PIL import Image, ImageChops, ImageStat

async_api = pytest.importorskip("playwright.async_api")

FULL = (0, 0, 800, 480)
KEY0 = (13, 12, 120, 120)
LOADED = (
    "(() => { const i = [...document.querySelectorAll('#pics img')];"
    " return i.length && i.every((x) => x.complete && x.naturalWidth); })()"
)


async def draw(bridge, background, key):
    await bridge.send_image(FULL, jpeg(background))
    await bridge.send_image(KEY0, jpeg(key, (120, 120)))


async def difference(page) -> float:
    """Share of pixels that clearly differ between the page's deck and /api/screen.jpg. Small colour shifts from
    encoding twice and the browser's colour handling do not count."""
    await asyncio.sleep(1)  # the change reaches the page over the web socket
    try:
        await page.wait_for_function(LOADED, timeout=3000)
    except async_api.TimeoutError:
        return 1.0  # still loading, or an image that is gone
    shot = Image.open(io.BytesIO(await page.locator("#deck").screenshot())).convert("RGB")
    truth = Image.open(io.BytesIO((await asyncio.to_thread(api, "GET", "/api/screen.jpg"))[1])).convert("RGB")
    diff = ImageChops.difference(shot.resize(truth.size), truth).convert("L").point(lambda v: 255 if v > 60 else 0)
    return ImageStat.Stat(diff).mean[0] / 255


async def settles(page, secs: float = 20) -> bool:
    """The page shows the deck again within `secs`, also when its socket died without a close."""
    for _ in range(int(secs)):
        if await difference(page) < 0.02:
            return True
    return False


async def login(context):
    await context.request.post(f"http://{HOST}/api/login", data=json.dumps({"password": PASSWORD}))


async def open_page(p):
    browser = await p.chromium.launch()
    context = await browser.new_context(viewport={"width": 1000, "height": 1400})
    await login(context)
    page = await context.new_page()
    await page.goto(f"http://{HOST}/")
    await page.wait_for_selector("#online.ok")
    return browser, page


async def live(server):
    """The link that is up now: an earlier test may have made the bridge connect again."""
    return list(server.bridges.values())[-1] if server.bridges else await server.wait_bridge(60)


async def test_preview_matches_the_deck(servers, bridges):
    active = await live(servers[0])
    async with async_api.async_playwright() as p:
        browser, page = await open_page(p)
        for background, key in [((200, 30, 30), (20, 200, 40)), ((30, 30, 200), (240, 200, 0))]:
            await draw(active, background, key)
            assert await difference(page) < 0.02
        await browser.close()


async def restart(server):
    while not server.connected.empty():
        server.connected.get_nowait()
    await asyncio.to_thread(api, "POST", "/api/reboot")
    await server.wait_bridge(90)
    await asyncio.sleep(3)  # both links connect, the bridge's own screens come first
    return list(server.bridges.values())[-1]


async def first_id() -> int:
    images = json.loads((await asyncio.to_thread(api, "GET", "/api/screen"))[1])["images"]
    return min(int(i["url"].rsplit("/", 1)[1].split(".")[0]) for i in images)


@pytest.mark.ota
async def test_preview_after_a_restart(servers):
    """Image URLs are cached as immutable, so an id must never name another picture after a restart. Two fresh
    boots that draw the same way give the same ids unless the ids start somewhere new each boot."""
    first, _ = servers
    bridge = await restart(first)
    async with async_api.async_playwright() as p:
        browser, page = await open_page(p)
        await draw(bridge, (200, 30, 30), (20, 200, 40))
        assert await difference(page) < 0.02
        before = await first_id()
        bridge = await restart(first)
        await draw(bridge, (30, 30, 200), (240, 200, 0))
        # how many images a boot draws varies, so a reused id is likely but not certain: check the cause itself
        assert abs(await first_id() - before) > 100_000
        # a restart ends the session: the page must drop the old picture for the login, then show the new one
        await page.wait_for_selector("#login", state="visible", timeout=60000)
        await login(page.context)
        await page.reload()
        assert await settles(page)
        await browser.close()
