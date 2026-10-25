#!/usr/bin/env bash
# WP-00 spike: builds the mode + memory probe (tools/spike/probe/main.c) into wp00-probe.nro.
# Uses devkitPro's standard libnx application template Makefile as-is (from switch-examples).
# Usage (inside WSL):  tools/spike/build-probe.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=~/build/suwayomi-nx/spike/probe
TEMPLATE=/opt/devkitpro/examples/switch/templates/application/Makefile

mkdir -p "$OUT/source"
cp "$TEMPLATE" "$OUT/Makefile"
cp "$HERE/probe/main.c" "$OUT/source/main.c"
make -C "$OUT" TARGET=wp00-probe APP_TITLE="suwayomi-nx WP-00 probe" APP_AUTHOR="suwayomi-nx" APP_VERSION=0.0.1 -j"$(nproc)"
echo "probe: $OUT/wp00-probe.nro"
sha256sum "$OUT/wp00-probe.nro"
