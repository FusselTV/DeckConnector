"""The README animation: the web UI and Home Assistant's camera, controls and events in one GIF, so they cannot drift
apart. Fails when a frame shows a different deck picture in the web UI and in Home Assistant, or when Home Assistant
shows a key or dial event more than one frame late.
uv run --with pillow python tools/media/readme.py <web frame dir from web-gif.mjs> <camera crop w:h:x:y> <out.gif>"""

import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

FPS = 12
WIDTH = 860
GAP = 10
BG = (17, 17, 17)  # Home Assistant's dark page
LATE = 0.5  # seconds
SEEN = 20  # mean grey difference under which a deck crop shows a pushed picture; a dimmed or hidden deck is above

DIR = Path(sys.argv[1])
CAMERA = tuple(int(v) for v in sys.argv[2].split(":"))
OUT = Path(sys.argv[3])
TL = json.loads((DIR / "timeline.json").read_text())


def track(name: str) -> tuple[list[tuple[float, Image.Image]], float]:
    frames, t, path = [], 0.0, None
    for line in (DIR / name / "list.txt").read_text().splitlines():
        if line.startswith("file"):
            path = line.split("'")[1]
        else:
            frames.append((t, Image.open(path).convert("RGB")))
            t += float(line.split()[1])
    return frames, t


def at(frames: list[tuple[float, Image.Image]], t: float) -> Image.Image:
    return next((im for s, im in reversed(frames) if s <= t + 1e-6), frames[0][1])


def crop(im: Image.Image, box) -> Image.Image:
    w, h, x, y = box
    return im.crop((x, y, x + w, y + h))


def small(im: Image.Image) -> Image.Image:
    return im.convert("L").resize((80, 48))


def diff(a: Image.Image, b: Image.Image) -> float:
    return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]


FULL = [Image.open(p).convert("L") for p in sorted(DIR.glob("screen-*.jpg"), key=lambda p: int(p.stem[7:]))]
if not FULL:
    sys.exit(f"no deck pictures in {DIR}")
PICTURES = [small(p) for p in FULL]
# Pictures that differ only in a dial value or the clock look alike when small: those are told apart in the part
# where they differ, at full size.
GROUP = [next(j for j in range(i + 1) if diff(PICTURES[i], PICTURES[j]) < 1) for i in range(len(PICTURES))]


def picture(im: Image.Image) -> int | None:
    d = [diff(small(im), p) for p in PICTURES]
    best = min(range(len(d)), key=d.__getitem__)
    if d[best] >= SEEN:
        return None
    alike = [i for i in range(len(FULL)) if GROUP[i] == GROUP[best]]
    boxes = [ImageChops.difference(FULL[alike[0]], FULL[i]).getbbox() for i in alike[1:]]
    boxes = [b for b in boxes if b]
    if not boxes:
        return alike[0]
    box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    big = im.convert("L").resize(FULL[0].size).crop(box)
    return min(alike, key=lambda i: diff(big, FULL[i].crop(box)))


web, total = track(".")
ha_tracks = {n: track(n)[0] for n in ("ha", "deck", "screen")}
x, y, w, h = TL["deck"]
deck_box = (w, h, x, y)
cx, cy, cw, ch = TL["camera"]
cam_box = (cw, ch, cx - CAMERA[2], cy - CAMERA[3])  # inside the camera crop


def changes(frames, box) -> list[tuple[float, int]]:
    out = []
    for t, im in frames:
        p = picture(crop(im, box))
        if p is not None and (not out or out[-1][1] != p):
            out.append((t, p))
    return out


# The camera shows each new deck picture 20 to 100 ms after the web UI, behind its MJPEG stream. It is shifted so
# each picture lands on the web UI's moment; more than LATE behind fails. Controls and Events are not shifted.
cam_track = [(t, crop(im, CAMERA)) for t, im in ha_tracks["screen"]]
anchors, ha_changes = [], changes(cam_track, cam_box)
for tw, p in changes(web, deck_box):
    k = next((k for k, (th, q) in enumerate(ha_changes) if q == p and th >= tw - 0.1), None)
    if k is None:
        continue
    if not -0.1 <= ha_changes[k][0] - tw < LATE:  # -0.1: screencast stamps jitter
        sys.exit(f"Home Assistant shows deck picture {p} {ha_changes[k][0] - tw:.2f} s after the web UI")
    anchors.append((tw, ha_changes[k][0] - tw))
    ha_changes = ha_changes[k + 1 :]
if not anchors:
    sys.exit("Home Assistant never showed the web UI's deck")


def cam_time(t: float) -> float:
    before = [a for a in anchors if a[0] <= t] or anchors[:1]
    after = [a for a in anchors if a[0] > t] or anchors[-1:]
    (t0, l0), (t1, l1) = before[-1], after[0]
    return t + (l0 if t1 <= t0 else l0 + (l1 - l0) * (t - t0) / (t1 - t0))


n = round(total * FPS)
grid = [i / FPS for i in range(n)]
webs = [at(web, t).crop((0, 0, 1280, 756)) for t in grid]
cams = [at(cam_track, cam_time(t)) for t in grid]
ctrls = [at(ha_tracks["ha"], t) for t in grid]
evs = [at(ha_tracks["deck"], t) for t in grid]

# layout: the web UI on top; below it the camera on the left and Controls over Events on the right, one height
web_h = round(756 * WIDTH / 1280)
c0, e0, k0 = cams[0].size, ctrls[0].size, evs[0].size
r_aspect = e0[1] / e0[0] + k0[1] / k0[0]  # right column height per width, without the gap
right_w = round((WIDTH - GAP - c0[0] / c0[1] * GAP) / (1 + c0[0] / c0[1] * r_aspect))
row_h = round(right_w * r_aspect + GAP)
left_w = WIDTH - GAP - right_w
ctrl_h = round(right_w * e0[1] / e0[0])


def fit(im: Image.Image, size) -> Image.Image:
    return im.resize(size, Image.LANCZOS)


frames = []
for i in range(n):
    f = Image.new("RGB", (WIDTH, web_h + GAP + row_h + GAP), BG)
    f.paste(fit(webs[i], (WIDTH, web_h)), (0, 0))
    f.paste(fit(cams[i], (left_w, row_h)), (0, web_h + GAP))
    f.paste(fit(ctrls[i], (right_w, ctrl_h)), (left_w + GAP, web_h + GAP))
    f.paste(fit(evs[i], (right_w, row_h - ctrl_h - GAP)), (left_w + GAP, web_h + GAP + ctrl_h + GAP))
    frames.append(f)

# the check runs on the source crops at full size: what the composite shows, without the scaling blur
bad, seen = [], 0
for i in range(n):
    a = picture(crop(webs[i], deck_box))
    seen += a is not None
    b = picture(crop(cams[i], cam_box))
    if a is not None and a != b:
        bad.append(f"frame {i}: web UI picture {a}, Home Assistant {b}")
labels = []
for t, what in TL["events"]:
    for e in what if isinstance(what, list) else []:
        label = (e["kind"], e["index"])
        # Home Assistant shows a repeat within a second as the same "Now"
        if not labels or label != labels[-1][1] or t - labels[-1][0] > 1:
            labels.append((t, label))
# each input needs a change of the Events card in its GIF frame or the next; the recorder keeps only changes
shown = [s for s, _ in ha_tracks["deck"][1:]]
for t, label in labels:
    if t + 1 / FPS > total:
        continue
    # -0.1: screenshot stamps jitter. Inputs closer than one screenshot apart share a change
    i = math.ceil(t * FPS)
    if not any(t - 0.1 <= s and math.ceil(s * FPS) <= i + 1 for s in shown):
        bad.append(f"frame {i}: Home Assistant shows {label} more than one frame late")
if seen < n / 3:
    bad.append(f"only {seen} of {n} frames show the web UI's deck: check the deck box")
if bad:
    sys.exit("out of sync:\n" + "\n".join(bad))

merged = []
for f in frames:
    if merged and not ImageChops.difference(f, merged[-1][0]).getbbox():
        merged[-1][1] += 1
    else:
        merged.append([f, 1])
picks = [f for f, _ in merged][:: max(1, len(merged) // 24)]
sample = Image.new("RGB", (WIDTH, frames[0].height * len(picks)))
for k, f in enumerate(picks):
    sample.paste(f, (0, k * f.height))
palette = sample.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
out = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f, _ in merged]
ms, end = [], 0
for _, k in merged:  # a GIF counts in 10 ms: rounding the sum keeps the length
    ms.append(10 * (round((end + k) * 100 / FPS) - round(end * 100 / FPS)))
    end += k
out[0].save(OUT, save_all=True, append_images=out[1:], duration=ms, loop=0, optimize=True, disposal=1)
behind = max(lag for _, lag in anchors) * 1000
print(f"{OUT}: {OUT.stat().st_size} bytes, {n} frames as {len(out)}, {sum(ms) / 1000:.1f} s, in sync")
print(f"Home Assistant was up to {behind:.0f} ms behind the web UI")
