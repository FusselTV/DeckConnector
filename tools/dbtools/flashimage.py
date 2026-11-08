"""Files for the browser flasher, from firmware/build: a factory image, the app-only update and manifest.json.

uv run python -m dbtools.flashimage [--build DIR] [--out DIR]
"""

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def app_info(app: bytes) -> dict:
    """Chip and version out of the app image header and its esp_app_desc_t (right after the first segment
    header, at 0x20)."""
    chip, _, rev_min, rev_max = struct.unpack_from("<HBHH", app, 12)
    magic, _, _, _, version, name, _, _, idf = struct.unpack_from("<IIII32s32s16s16s32s", app, 0x20)
    if app[0] != 0xE9 or magic != 0xABCD5432:
        raise ValueError("not an ESP app image")
    text = lambda b: b.rstrip(b"\0").decode()  # noqa: E731
    return {
        "chip_id": chip,
        "chip_revision": {"min": rev_min, "max": rev_max},  # major * 100 + minor, as esptool shows v3.1 = 301
        "version": text(version),
        "project": text(name),
        "idf": text(idf),
    }


def part(path: Path, offset: int, data: bytes) -> dict:
    return {"path": path.name, "offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def build(src: Path, out: Path) -> dict:
    args = json.loads((src / "flasher_args.json").read_text())
    files = {int(off, 16): src / name for off, name in args["flash_files"].items()}
    app_off = int(args["app"]["offset"], 16)
    otadata_off = int(args["otadata"]["offset"], 16)
    info = app_info(files[app_off].read_bytes())
    extra = args["extra_esptool_args"]

    # Blank board: bootloader 0x2000, partition table 0x8000, otadata 0xf000 (boot ota_0), app in ota_0 at
    # 0x20000, all from 0x0 with 0xff in the gaps. The gaps are NVS (0x9000) and phy_init (0x11000), so this
    # also wipes the settings. Nothing goes to the `www` partition: empty, the firmware serves its built-in
    # page. The ESP32-C6 is not reachable over this port; its firmware comes as a .dbfw update in the web UI.
    end = max(off + f.stat().st_size for off, f in files.items())
    factory = bytearray(b"\xff" * end)
    for off, f in files.items():
        data = f.read_bytes()
        factory[off : off + len(data)] = data
    out.mkdir(parents=True, exist_ok=True)
    factory_path = out / "deckconnector-factory.bin"
    factory_path.write_bytes(factory)

    # Update: like tools/flash.sh app, otadata too, or after a web update the bootloader keeps the other slot.
    update = []
    for off in (otadata_off, app_off):
        shutil.copyfile(files[off], out / files[off].name)
        update.append(part(files[off], off, files[off].read_bytes()))

    manifest = {
        "name": "DeckConnector",
        **info,
        "chip": extra["chip"],
        # the bootloader's header already carries these; esptool-js keeps them with flashMode "keep"
        "flash": args["flash_settings"],
        "esptool": {
            "stub": extra["stub"],
            "baud": 921600,
            "baud_fallback": 115200,
            "before": extra["before"],
            "after": extra["after"],
        },
        "console": {"baud": 115200, "improv": True},
        "builds": {
            "factory": {"erase_all": True, "parts": [part(factory_path, 0, bytes(factory))]},
            "update": {"erase_all": False, "parts": update},
        },
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", type=Path, default=ROOT / "firmware/build")
    ap.add_argument("--out", type=Path, default=ROOT / "firmware/build/web-flash")
    a = ap.parse_args()
    m = build(a.build, a.out)
    for name, b in m["builds"].items():
        print(name, ", ".join(f"{p['path']} @ {p['offset']:#x} ({p['size']} bytes)" for p in b["parts"]))
    print(f"{a.out}/manifest.json: {m['version']}, {m['chip']} rev {m['chip_revision']}")


if __name__ == "__main__":
    main()
