"""PNGs from ha-shots.mjs to the docs files.
uv run --with pillow python tools/media/webp.py <png dir>"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

for src in sorted(Path(sys.argv[1]).glob("ha-*-*-*.png")):
    name, lang, theme = src.stem.rsplit("-", 2)
    out = ROOT / "site/public/docs" / f"{name}{'' if lang == 'en' else '-de'}-{theme}.webp"
    Image.open(src).convert("RGBA").save(out, quality=88, method=6)
    print(out.relative_to(ROOT), out.stat().st_size)
