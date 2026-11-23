"""A host that draws only its keys must not show what the previous host painted around them."""

import asyncio

from conftest import camera_frame, paint

KEYS_IN_CAMERA = (760, 220, 1660, 580)  # this bench's camera framing


def red_pixels() -> int:
    img = camera_frame().crop(KEYS_IN_CAMERA)
    px, (w, h) = img.load(), img.size
    return sum(1 for x in range(0, w, 2) for y in range(0, h, 2) if px[x, y][0] > 170 and max(px[x, y][1:]) < 70)


async def test_a_new_host_starts_on_a_black_display(bridges):
    painter, keys = bridges
    assert (await paint(painter, (230, 0, 0))).us > 0
    await asyncio.sleep(1)
    try:
        await keys.role(True)
        for n in range(8):
            assert (await paint(keys, (0, 0, 0), "key", n)).us > 0, keys.errors
        await asyncio.sleep(1)
        red = await asyncio.to_thread(red_pixels)
    finally:
        await painter.role(True)
    assert red < 30, f"{red} red pixels around the keys: the previous host's picture is still there"
