#!/usr/bin/env bash
# Flash over the RFC2217 serial bridge (serial in the bench file, dbtools.site) and keep a serial log in /tmp/esp-live.log.
# Usage: flash.sh [app|all]   app writes only the running slot, all writes bootloader and partition table too.
set -euo pipefail
PORT=$(cd "$(dirname "$0")" && uv run --quiet python -m dbtools.site serial)
[ -n "$PORT" ] || { echo "set serial in ~/.config/deckbridge/bench.json" >&2; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD="$HERE/../firmware/build"

pkill -f "dbtools.serlog" || true
sleep 1
# shellcheck disable=SC1091
. ~/esp/esp-idf/export.sh >/dev/null 2>&1
cd "$BUILD"
if [ "${1:-app}" = all ]; then
    esptool --chip esp32p4 --port "$PORT" -b 921600 --after no-reset write-flash $(tr '\n' ' ' < flash_args)
else
    # otadata too: after an OTA update the bootloader would otherwise keep starting the other slot
    esptool --chip esp32p4 --port "$PORT" -b 921600 --after no-reset write-flash 0xf000 ota_data_initial.bin 0x20000 deckconnector.bin
fi
cd "$HERE"
setsid nohup uv run --quiet python -m dbtools.serlog --reset --secs 86400 > /tmp/esp-live.log 2>&1 < /dev/null &
echo "flashed, log in /tmp/esp-live.log"
