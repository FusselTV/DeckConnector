"""How late the camera shows the deck. Anything timed with the camera must subtract this delay."""

import asyncio
import statistics
import time

from conftest import camera, paint


async def test_camera_delay(bridges):
    active, _ = bridges
    cam = camera()
    for _ in range(100):  # a stream that just started has no frame yet
        if cam.frames:
            break
        await asyncio.sleep(0.1)
    delays = []
    for i in range(4):
        color, channel = ((220, 20, 20), 0) if i % 2 else ((20, 20, 220), 2)
        await paint(active, color)  # SHOWN comes once the image left over USB
        drawn = time.monotonic()
        seen = await asyncio.to_thread(cam.first_after, drawn, channel)
        assert seen, f"the camera never showed {color}"
        delays.append(seen - drawn)
        await asyncio.sleep(0.5)
    print(
        f"camera and display delay: median {statistics.median(delays) * 1000:.0f} ms, "
        f"from {min(delays) * 1000:.0f} to {max(delays) * 1000:.0f} ms"
    )
    assert statistics.median(delays) < 2
