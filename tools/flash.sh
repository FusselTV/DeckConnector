#!/usr/bin/env bash
# Flash over the serial bridge (serial bridge) and keep a serial log in /tmp/esp-live.log.
# Usage: flash.sh [app|all]   app writes only the running slot, all writes bootloader and partition table too.
set -euo pipefail
PORT='rfc2217://192.168.1.170:4000?ign_set_control'
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
    esptool --chip esp32p4 --port "$PORT" -b 921600 --after no-reset write-flash 0xf000 ota_data_initial.bin 0x20000 deckbridge.bin
fi
cd "$HERE"
setsid nohup uv run --quiet python -m dbtools.serlog --reset --secs 86400 > /tmp/esp-live.log 2>&1 < /dev/null &
echo "flashed, log in /tmp/esp-live.log"
