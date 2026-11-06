"""A host that draws only its keys must not show what the previous host painted around them."""

import asyncio

from conftest import camera_frame, jpeg, newest, put_config

FULL = (0, 0, 800, 480)
KEYS = [(13 + 219 * (i % 4), 12 + 160 * (i // 4), 120, 120) for i in range(8)]
KEYS_IN_CAMERA = (760, 220, 1660, 580)  # this bench's camera framing


def red_pixels() -> int:
    img = camera_frame().crop(KEYS_IN_CAMERA)
    px, (w, h) = img.load(), img.size
    return sum(1 for x in range(0, w, 2) for y in range(0, h, 2) if px[x, y][0] > 170 and max(px[x, y][1:]) < 70)


async def test_a_new_host_starts_on_a_black_display(servers, bridges):
    _, second = servers
    painter, _ = bridges
    assert (await painter.send_image(FULL, jpeg((230, 0, 0)))).result.name == "SHOWN"
    await asyncio.sleep(1)
    try:
        await asyncio.to_thread(put_config, {"host": second.url})
        keys = await newest(second)
        for _ in range(100):
            if keys.active:
                break
            await asyncio.sleep(0.05)
        for rect in KEYS:
            assert (await keys.send_image(rect, jpeg((0, 0, 0), (120, 120)))).result.name == "SHOWN"
        await asyncio.sleep(1)
        red = await asyncio.to_thread(red_pixels)
    finally:
        await asyncio.to_thread(put_config, {"host": ""})
    assert red < 30, f"{red} red pixels around the keys: the previous host's picture is still there"
