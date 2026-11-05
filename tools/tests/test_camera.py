"""How late the camera shows the deck. Anything timed with the camera must subtract this delay."""

import asyncio
import statistics
import time

from conftest import CameraStream, jpeg

FULL = (0, 0, 800, 480)


async def test_camera_delay(bridges):
    active, _ = bridges
    cam = CameraStream()
    try:
        await asyncio.sleep(3)  # the stream starts with a key frame
        delays = []
        for i in range(8):
            color, channel = ((220, 20, 20), 0) if i % 2 else ((20, 20, 220), 2)
            await active.send_image(FULL, jpeg(color))  # the ack comes once the image left over USB
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
    finally:
        cam.close()
