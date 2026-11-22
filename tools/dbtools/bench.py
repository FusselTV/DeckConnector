"""Benchmarks and the interactive input demo, as a surface host on port 8765. Pairs the bridge (--host), makes it
draw for this host and sets its settings back at the end.

bench react                 latency for one key, the strip and the full screen
bench video --fps 30        full screen video as STREAM; --flow sends only when the last frame was SHOWN
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

from . import protocol as p
from .load import Client, basic_auth, drawing_for
from .protocol import InputKind
from .server import Server
from .site import HOST

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
    font = ImageFont.truetype(BOLD, 48)
    for kind in ("key", "strip", "full"):
        t = bridge.target(0, kind)
        times, glass = [], []
        for i in range(args.n):
            img = Image.new("RGB", (t["w"], t["h"]), (30, 40, 60))
            ImageDraw.Draw(img).text((t["w"] // 2, t["h"] // 2), f"{i}%", font=font, fill="white", anchor="mm")
            data = jpeg(img, args.q)
            sent = time.perf_counter()
            shown = await bridge.draw(0, t, data, t["id"])
            times.append((time.perf_counter() - sent) * 1000)
            glass.append(shown.us / 1000)
            await asyncio.sleep(0.03)
        print(
            f"{kind:8s} {len(data) / 1024:5.1f} KB  PUT and FRAME to SHOWN: median {statistics.median(times):.2f} ms, "
            f"p95 {pct(times, 0.95):.2f}, max {max(times):.2f}; SHOWN.us median {statistics.median(glass):.2f} ms"
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
    full = bridge.target(0, "full")["id"]
    pending, shown, skipped, sizes = None, [], 0, []
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
            sent = time.perf_counter()
            pending = await bridge.stream(0, full, data)
            pending.add_done_callback(lambda f, sent=sent: shown.append((sent, time.perf_counter(), f.result())))
        nxt += 1 / args.fps
        await asyncio.sleep(max(0, nxt - time.monotonic()))
    await asyncio.sleep(1.5)
    drawn = [(s, t) for s, t, r in shown if r.us]
    lat = [(t - s) * 1000 for s, t in drawn]
    span = drawn[-1][1] - drawn[0][1] if len(drawn) > 1 else 1
    print(
        f"video {'flow' if args.flow else 'blast'} {args.fps} fps q{args.q}: sent {len(sizes)}, "
        f"{statistics.mean(sizes) / 1024:.1f} KB each, skipped by server {skipped}, "
        f"replaced by a newer one {len(shown) - len(drawn)}, "
        f"on the device {(len(drawn) - 1) / span:.1f} fps, send to SHOWN median {statistics.median(lat):.1f} ms, "
        f"p95 {pct(lat, 0.95):.1f} ms"
    )


class Demo:
    """Shows every input on the device and how long it took from its INPUT arriving here to SHOWN."""

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
            f"Eingabe bis angezeigt {self.lat[-1]:.1f} ms (Median {statistics.median(last):.1f}, max {max(last):.1f})"
        )

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
        """(target, image) pairs. The strip has no quarters of its own: mode 2 redraws it only when it changed."""
        b = self.bridge
        if self.mode == 0 or 7 in keys:
            return [(b.target(0, "full"), self.full_img())]
        out = [(b.target(0, "key", k), self.key_img(k)) for k in sorted(keys)]
        if self.mode == 1 or encs:
            out.append((b.target(0, "strip"), self.full_img().crop((0, 380, 800, 480))))
        return out

    async def draw(self, regs) -> asyncio.Future[p.Shown]:
        for t, img in regs:
            await self.bridge.put(t["id"], jpeg(img, self.q), t["w"], t["h"])
        return await self.bridge.frame(0, [(t["id"], t["id"]) for t, _ in regs])

    async def run(self, secs):
        b = self.bridge
        await self.draw(self.regions(set(), set()))
        end = time.monotonic() + secs
        while time.monotonic() < end:
            try:
                first = await asyncio.wait_for(b.inputs.get(), 1)
                got = time.perf_counter()
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
            shown = await asyncio.wait_for(await self.draw(regs), 5)
            if shown.us:
                self.lat.append((time.perf_counter() - got) * 1000)
                print(f"{self.MODES[self.mode]} {len(regs)} areas: input to SHOWN {self.lat[-1]:.2f} ms", flush=True)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["react", "video", "demo"])
    ap.add_argument("--n", type=int, default=50)
    ap.add_argument("--q", type=int, default=80)
    ap.add_argument("--fps", type=float, default=30)
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--flow", action="store_true")
    ap.add_argument("--host", default=HOST)
    args = ap.parse_args()
    server = await Server(8765).start()
    try:
        async with drawing_for(Client(args.host, basic_auth()), server) as bridge:
            if args.cmd == "react":
                await react(bridge, args)
            elif args.cmd == "video":
                await video(bridge, args)
            else:
                await Demo(bridge, args.q).run(args.secs)
    finally:
        await server.stop()


if __name__ == "__main__":
    asyncio.run(main())
