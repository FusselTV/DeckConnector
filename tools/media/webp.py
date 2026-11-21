"""PNGs from ha-shots.mjs to the docs files, plus contact sheets to look at.
uv run --with pillow python tools/media/webp.py <png dir>"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(sys.argv[1])


def save(src: Path, out: Path, rounded: bool = False) -> None:
    im = Image.open(src).convert("RGBA")
    if rounded:  # a dialog has round corners; the dimmed page behind them must not show
        mask = Image.new("L", (im.width * 4, im.height * 4), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, mask.width - 1, mask.height - 1), radius=48 * 4, fill=255)
        alpha = mask.resize(im.size, Image.LANCZOS)
        # the corner pixels take the dialog's colour: half transparent, the dimmed page in them shows as a grey edge
        im = Image.composite(
            im, Image.new("RGBA", im.size, im.getpixel((im.width // 2, 4))), alpha.point(lambda a: 255 * (a == 255))
        )
        im.putalpha(alpha)
    im.save(out, quality=88, method=6)
    print(out.relative_to(ROOT), im.size, out.stat().st_size)


for src in sorted(SRC.glob("ha-*-*-*.png")):
    name, lang, theme = src.stem.rsplit("-", 2)
    save(src, ROOT / "site/public/docs" / f"{name}{'' if lang == 'en' else '-de'}-{theme}.webp", name == "ha-flow")

for variant in ("en-dark", "en-light", "de-dark", "de-light"):
    ims = [Image.open(SRC / f"ha-{n}-{variant}.png") for n in ("board", "deck", "dashboard", "flow")]
    sheet = Image.new("RGB", (sum(i.width for i in ims) + 30, max(i.height for i in ims)), "magenta")
    x = 0
    for im in ims:
        sheet.paste(im, (x, 0))
        x += im.width + 10
    sheet.thumbnail((2400, 2400))
    sheet.save(SRC / f"sheet-{variant}.png")
