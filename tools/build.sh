#!/usr/bin/env bash
# Firmware build for the edit loop: plain ninja while the build files are current (2 s less than export.sh + idf.py),
# idf.py when CMake has to run, because that needs the full IDF environment.
set -e
export PATH=$HOME/.local/bin:$PATH  # ccache
fw=$(cd "$(dirname "$0")/../firmware" && pwd)
ninja=$(ls -d "$HOME"/.espressif/tools/ninja/*/ninja | tail -1)
if [ -f "$fw/build/build.ninja" ] && "$ninja" -C "$fw/build" -n build.ninja | grep -q "no work to do"; then
    exec "$ninja" -C "$fw/build" "$@"
fi
. "$HOME/esp/esp-idf/export.sh" >/dev/null
exec idf.py -C "$fw" build
