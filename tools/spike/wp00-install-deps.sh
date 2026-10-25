#!/usr/bin/env bash
# WP-00: the extra packages the borealis demo needs (on top of WP-00a's base install).
# Run it once in WSL. It asks for your sudo password one time.
#   desktop (Linux/WSLg): the same list borealis's own CI installs
#   Switch (deko3d):      switch-glm, a header-only maths library deko3d's nanovg renderer includes
set -euo pipefail
LOG="$HOME/build/suwayomi-nx/wp00-deps.log"
{
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends libdbus-1-dev libxkbcommon-dev wayland-protocols xorg-dev
    sudo dkp-pacman -S --needed --noconfirm switch-glm
    echo "WP00_DEPS_OK"
} 2>&1 | tee "$LOG"
echo
echo "Done. You can close this tab."
