#!/usr/bin/env bash
# WP-00 spike: builds xfangfang/borealis's demo for the Switch (deko3d) and the desktop (GLFW + OpenGL 3).
# Throwaway: WP-01 replaces it with our own CMake project and a borealis submodule.
#
# One change to the stock demo: PLUS quits (setGlobalQuit(true)). Stock has it off, and then only
# HOME -> Close can end the demo.
#
# Usage (inside WSL):  tools/spike/build-demo.sh [switch|desktop|all]   (default: all)
set -euo pipefail

BOREALIS_URL=https://github.com/xfangfang/borealis.git
BOREALIS_COMMIT=5f08b286f3df737f3321d2247a6fe633fcead03c   # main on 2026-04-25, used in WP-00
SPIKE=~/build/suwayomi-nx/spike
SRC=$SPIKE/borealis
OUT=$SPIKE/borealis-build
WHAT="${1:-all}"

if [ ! -d "$SRC/.git" ]; then
    git clone --recurse-submodules "$BOREALIS_URL" "$SRC"
fi
git -C "$SRC" checkout -q "$BOREALIS_COMMIT"
git -C "$SRC" submodule update --init --recursive -q

# The one patch (idempotent).
sed -i 's/brls::Application::setGlobalQuit(false);/brls::Application::setGlobalQuit(true);/' "$SRC/demo/src/main.cpp"
grep -q 'setGlobalQuit(true)' "$SRC/demo/src/main.cpp"

if [ "$WHAT" = all ] || [ "$WHAT" = switch ]; then
    cmake -S "$SRC" -B "$OUT/switch" -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON > "$OUT/switch-cmake.log"
    make -C "$OUT/switch" borealis_demo.nro -j"$(nproc)" > "$OUT/switch.log" 2>&1 || { tail -30 "$OUT/switch.log"; exit 1; }
    echo "switch:  $OUT/switch/borealis_demo.nro"
    sha256sum "$OUT/switch/borealis_demo.nro"
fi
if [ "$WHAT" = all ] || [ "$WHAT" = desktop ]; then
    cmake -S "$SRC" -B "$OUT/desktop" -DCMAKE_BUILD_TYPE=Release -DPLATFORM_DESKTOP=ON > "$OUT/desktop-cmake.log"
    make -C "$OUT/desktop" -j"$(nproc)" > "$OUT/desktop.log" 2>&1 || { tail -30 "$OUT/desktop.log"; exit 1; }
    echo "desktop: $OUT/desktop/borealis_demo (run it from $OUT/desktop, it needs ./resources)"
fi
