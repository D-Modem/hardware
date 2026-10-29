#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 D-Modem Authors
# End-to-end test of a firmware image in the rp2040js emulator.
#   firmware/test/emu/run.sh build/pico-serial-hood.bin
# Needs Node.js 20+ and network on first run (npm, and the RP2040 boot ROM
# from the rp2040js repository at a pinned commit).
set -e
here=$(cd "$(dirname "$0")" && pwd)
image=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$here"
[ -d node_modules/rp2040js ] || npm install --no-audit --no-fund --silent
if [ ! -f vendor/bootrom.ts ]; then
    mkdir -p vendor
    curl -fsSL -o vendor/bootrom.ts \
        https://raw.githubusercontent.com/wokwi/rp2040js/a304c7486f329dae64be5f65baf28795d62ceb95/demo/bootrom.ts
fi
exec npx tsx emu_test.ts "$image"
