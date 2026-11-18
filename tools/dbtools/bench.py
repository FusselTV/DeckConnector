"""Benchmarks and the interactive input demo, as a deckbridge server on port 8765.

bench react                 latency for one key, a strip quarter, the strip and the full screen
bench video --fps 30        full screen video; --flow sends only when the last frame was acknowledged
bench demo                  draws every input; key 8 switches between full, partial and minimal updates
"""

import argparse
import asyncio
import colorsys
import io
import statistics
import subprocess
import time

from PIL import Image, ImageDraw, ImageFont

from .protocol import AckResult, InputKind
from .server import Server, now_us

BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
REGULAR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def jpeg(img: Image.Image, q: int) -> bytes:
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=q)
    return buf.getvalue()


def pct(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))]


async def react(bridge, args):
    offset = await bridge.clock_offset()
    font = ImageFont.truetype(BOLD, 48)
    for name, rect in (
        ("key", (13, 12, 120, 120)),
        ("quarter", (200, 380, 200, 100)),
        ("strip", (0, 380, 800, 100)),
        ("full", (0, 0, 800, 480)),
    ):
        times = []
        for i in range(args.n):
            img = Image.new("RGB", rect[2:], (30, 40, 60))
            ImageDraw.Draw(img).text((rect[2] // 2, rect[3] // 2), f"{i}%", font=font, fill="white", anchor="mm")
            data = jpeg(img, args.q)
            sent = now_us()
            ack = await bridge.send_image(rect, data)
            times.append((ack.t_done - offset - sent) / 1000)
            await asyncio.sleep(0.03)
        print(
            f"{name:8s} {len(data) / 1024:5.1f} KB  send to USB done: median {statistics.median(times):.2f} ms, "
            f"p95 {pct(times, 0.95):.2f}, max {max(times):.2f}"
        )


def frames(fps: float):
    cmd = [
        "ffmpeg",
        "-loglevel",
        "error",
        "-f",
        "lavfi",
        "-i",
        f"testsrc2=s=800x480:r={fps}",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    assert proc.stdout
    while True:
        raw = proc.stdout.read(800 * 480 * 3)
        if len(raw) < 800 * 480 * 3:
            return
        yield Image.frombytes("RGB", (800, 480), raw)


async def video(bridge, args):
    offset = await bridge.clock_offset()
    pending, acks, skipped, sizes = None, [], 0, []
    start = time.monotonic()
    nxt = start
    for frame in frames(args.fps):
        if time.monotonic() - start > args.secs:
            break
        if args.flow and pending and not pending.done():
            skipped += 1
        else:
            data = jpeg(frame, args.q)
            sizes.append(len(data))
            sent = now_us()
            pending = await bridge.post_image((0, 0, 800, 480), data)
            pending.add_done_callback(lambda f, sent=sent: acks.append((sent, f.result())))
        nxt += 1 / args.fps
        await asyncio.sleep(max(0, nxt - time.monotonic()))
    await asyncio.sleep(1.5)
    shown = [(s, a) for s, a in acks if a.result == AckResult.SHOWN]
    lat = [(a.t_done - offset - s) / 1000 for s, a in shown]
    span = (shown[-1][1].t_done - shown[0][1].t_done) / 1e6 if len(shown) > 1 else 1
    print(
        f"video {'flow' if args.flow else 'blast'} {args.fps} fps q{args.q}: sent {len(sizes)}, "
        f"{statistics.mean(sizes) / 1024:.1f} KB each, skipped by server {skipped}, "
        f"superseded {sum(a.result == AckResult.SUPERSEDED for _, a in acks)}, "
        f"on the device {(len(shown) - 1) / span:.1f} fps, latency median {statistics.median(lat):.1f} ms, "
        f"p95 {pct(lat, 0.95):.1f} ms"
    )


class Demo:
    """Shows every input on the device and how long it took, measured on the bridge clock."""

    MODES = ("Voll", "Teil", "Mini")
    KEYS_X = (13, 232, 451, 670)
    KEYS_Y = (12, 172)

    def __init__(self, bridge, q):
        self.bridge, self.q = bridge, q
        self.enc, self.enc_down, self.keys = [0] * 4, [False] * 4, [False] * 8
        self.mode, self.action, self.lat = 0, "Bereit: drehen oder drücken", []
        self.big, self.mid, self.small = (
            ImageFont.truetype(BOLD, 40),
            ImageFont.truetype(BOLD, 24),
            ImageFont.truetype(REGULAR, 18),
        )

    def colors(self):
        rgb = colorsys.hsv_to_rgb((self.enc[0] % 100) / 100, 0.7, 0.45)
        bg = tuple(int(v * 255) for v in rgb)
        return bg, tuple(v // 2 for v in bg)

    def key_img(self, k):
        _, dim = self.colors()
        img = Image.new("RGB", (120, 120), (255, 200, 0) if self.keys[k] else dim)
        d = ImageDraw.Draw(img)
        fg = "black" if self.keys[k] else "white"
        d.text((60, 52), str(k + 1), font=self.big, fill=fg, anchor="mm")
        if k == 7:
            d.text((60, 96), self.MODES[self.mode], font=self.small, fill=fg, anchor="mm")
        return img

    def lat_text(self):
        if not self.lat:
            return "Latenz: noch keine Messung"
        last = self.lat[-30:]
        return (
            f"Eingabe bis USB fertig {self.lat[-1]:.1f} ms (Median {statistics.median(last):.1f}, max {max(last):.1f})"
        )

    def quarter_img(self, i):
        _, dim = self.colors()
        img = Image.new("RGB", (200, 100), dim)
        d = ImageDraw.Draw(img)
        d.text((100, 28), f"R{i + 1}: {self.enc[i]}", font=self.mid, fill="white", anchor="mm")
        d.text((100, 60), f"{self.lat[-1]:.1f} ms" if self.lat else "-", font=self.small, fill="white", anchor="mm")
        d.rectangle(
            (10, 80, 10 + int(180 * (self.enc[i] % 100) / 100), 94),
            fill=(255, 200, 0) if self.enc_down[i] else (0, 150, 230),
        )
        return img

    def full_img(self):
        bg, dim = self.colors()
        img = Image.new("RGB", (800, 480), bg)
        for k in range(8):
            img.paste(self.key_img(k), (self.KEYS_X[k % 4], self.KEYS_Y[k // 4]))
        d = ImageDraw.Draw(img)
        d.rectangle((0, 380, 800, 480), fill=dim)
        d.text((400, 400), self.action, font=self.mid, fill="white", anchor="mm")
        d.text((400, 430), self.lat_text(), font=self.small, fill="white", anchor="mm")
        for i in range(4):
            d.rectangle(
                (i * 200 + 10, 448, i * 200 + 10 + int(180 * (self.enc[i] % 100) / 100), 474),
                fill=(255, 200, 0) if self.enc_down[i] else (0, 150, 230),
            )
            d.text((i * 200 + 100, 461), f"R{i + 1}: {self.enc[i]}", font=self.small, fill="white", anchor="mm")
        return img

    def apply(self, ev):
        """Update the state; returns which keys and encoders changed."""
        keys, encs = set(), set()
        if ev.kind in (InputKind.KEY_DOWN, InputKind.KEY_UP):
            self.keys[ev.index] = ev.kind == InputKind.KEY_DOWN
            keys.add(ev.index)
            self.action = f"Taste {ev.index + 1} {'gedrückt' if self.keys[ev.index] else 'losgelassen'}"
            if ev.index == 7 and ev.kind == InputKind.KEY_DOWN:
                self.mode = (self.mode + 1) % 3
                self.lat.clear()
                self.action = "Modus: " + ("Vollbild", "Streifen und Taste", "nur was sich ändert")[self.mode]
        elif ev.kind == InputKind.ENCODER_TURN:
            self.enc[ev.index] += ev.value
            encs.add(ev.index)
            self.action = f"Regler {ev.index + 1} {ev.value:+d}  →  {self.enc[ev.index]}"
        elif ev.kind in (InputKind.ENCODER_DOWN, InputKind.ENCODER_UP):
            self.enc_down[ev.index] = ev.kind == InputKind.ENCODER_DOWN
            encs.add(ev.index)
            self.action = f"Regler {ev.index + 1} {'gedrückt' if self.enc_down[ev.index] else 'losgelassen'}"
        elif ev.kind in (InputKind.TOUCH_TAP, InputKind.TOUCH_LONG):
            encs.add(min(3, ev.x // 200))
            self.action = f"Streifen {'getippt' if ev.kind == InputKind.TOUCH_TAP else 'lang gedrückt'} bei x={ev.x}"
        elif ev.kind == InputKind.TOUCH_SWIPE:
            self.action = f"Gewischt von x={ev.x} nach x={ev.x2}"
        return keys, encs

    def regions(self, keys, encs):
        if self.mode == 0 or 7 in keys:
            return [((0, 0, 800, 480), self.full_img())]
        out = [((self.KEYS_X[k % 4], self.KEYS_Y[k // 4], 120, 120), self.key_img(k)) for k in sorted(keys)]
        if self.mode == 1:
            return [*out, ((0, 380, 800, 100), self.full_img().crop((0, 380, 800, 480)))]
        return out + [((i * 200, 380, 200, 100), self.quarter_img(i)) for i in sorted(encs)]

    async def run(self, secs):
        b = self.bridge
        await b.send_image((0, 0, 800, 480), jpeg(self.full_img(), self.q))
        end = time.monotonic() + secs
        while time.monotonic() < end:
            try:
                first = await asyncio.wait_for(b.inputs.get(), 1)
            except TimeoutError:
                continue
            events = [first]
            while not b.inputs.empty():  # fast turning: draw only the newest state
                events.append(b.inputs.get_nowait())
            keys, encs = set(), set()
            for ev in events:
                k, e = self.apply(ev)
                keys |= k
                encs |= e
            regs = self.regions(keys, encs)
            futures = [await b.post_image(r, jpeg(img, self.q)) for r, img in regs]
            ack = await asyncio.wait_for(futures[-1], 5)
            if ack.result == AckResult.SHOWN:
                self.lat.append((ack.t_done - events[-1].t) / 1000)
                print(f"{self.MODES[self.mode]} {len(regs)} areas: input to USB done {self.lat[-1]:.2f} ms", flush=True)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["react", "video", "demo"])
    ap.add_argument("--n", type=int, default=50)
    ap.add_argument("--q", type=int, default=80)
    ap.add_argument("--fps", type=float, default=30)
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--flow", action="store_true")
    args = ap.parse_args()
    server = await Server(8765).start()
    bridge = await server.wait_bridge(60)
    await asyncio.sleep(0.3)
    if args.cmd == "react":
        await react(bridge, args)
    elif args.cmd == "video":
        await video(bridge, args)
    else:
        await Demo(bridge, args.q).run(args.secs)
    await server.stop()


if __name__ == "__main__":
    asyncio.run(main())
