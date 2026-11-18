#!/bin/sh
# clang-tidy over the firmware outside db_core (the lint job covers that), with the compile database of an ESP-IDF
# build. clang knows neither Espressif's CPU extensions nor picolibc's specs file, so those flags are swapped for
# plain RISC-V ones and the include paths the toolchain uses.
set -e
build=${1:-firmware/build}
inc=$(riscv32-esp-elf-gcc @"$build/toolchain/cflags" -E -Wp,-v -xc /dev/null 2>&1 | sed -n 's/^ \(\/.*\)/-isystem \1/p' | tr '\n' ' ')
mkdir -p "$build/tidy"
sed -E "s#@\\\\\"[^ ]*toolchain/cflags\\\\\"#--target=riscv32-unknown-elf -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -nostdinc $inc#g; s# -f(no-tree-switch-conversion|strict-volatile-bitfields|no-malloc-dce|zero-init-padding-bits=all|no-jump-tables|no-shrink-wrap)##g" \
    "$build/compile_commands.json" > "$build/tidy/compile_commands.json"
git ls-files 'firmware/components/db_*/*.c' 'firmware/main/*.c' | grep -v -e font_data.c -e '^firmware/components/db_core/' -e db_audio_none.c |
    xargs -P"$(nproc)" -n1 clang-tidy -p "$build/tidy" --quiet --extra-arg=-Wno-unknown-warning-option
