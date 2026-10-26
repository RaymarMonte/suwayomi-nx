#!/usr/bin/env bash
# Desktop smoke test (WP-01; CI's desktop job still uses it, D32. The E2E harness's smoke scenario does more: tools/e2e.sh desktop smoke): starts the desktop build on a virtual X screen (Xvfb),
# waits until the log says the setup screen (first run, WP-08) has the focus, saves a screenshot, checks the app is still running, stops it.
# Runs inside WSL and in CI. Exit code 0 = pass.
#
# Usage:  tools/desktop-smoke.sh [build dir]   (default ~/build/suwayomi-nx/desktop)
# Output: <build dir>/smoke/screenshot.png and app.log
#
# WSLg note: GLFW prefers Wayland and finds WSLg's socket through XDG_RUNTIME_DIR even without WAYLAND_DISPLAY, which puts
# a window on the owner's screen. So this script gives the app an empty runtime folder, and GLFW falls back to X11 (Xvfb).
# Desktop borealis maps no keyboard key to PLUS, so the script stops the app with SIGTERM.
set -euo pipefail

BUILD="${1:-$HOME/build/suwayomi-nx/desktop}"
OUT="$BUILD/smoke"
DISPLAY_NUM="${SNX_XVFB_DISPLAY:-:97}"
EXPECT='Giving focus to snx::ui::TextFieldCell (id="setup/url")'

[ -x "$BUILD/suwayomi-nx" ] || { echo "no desktop build in $BUILD (run tools/build.sh desktop)"; exit 1; }
rm -rf "$OUT" "$BUILD/data"
mkdir -p "$OUT"
RUNTIME_DIR="$(mktemp -d)"
XVFB_PID=""; APP_PID=""
cleanup() {
    [ -n "$APP_PID" ] && kill "$APP_PID" 2> /dev/null || true
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2> /dev/null || true
    rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT

Xvfb "$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp > "$OUT/xvfb.txt" 2>&1 &
XVFB_PID=$!
sleep 1

cd "$BUILD"
env -u WAYLAND_DISPLAY DISPLAY="$DISPLAY_NUM" XDG_RUNTIME_DIR="$RUNTIME_DIR" ./suwayomi-nx -d --log-file > "$OUT/stdout.txt" 2>&1 &
APP_PID=$!

LOG="$BUILD/data/logs/app.log"
for _ in $(seq 1 100); do   # up to 20 s (software OpenGL is slow on CI machines)
    if grep -qF "$EXPECT" "$LOG" 2> /dev/null; then break; fi
    kill -0 "$APP_PID" 2> /dev/null || break
    sleep 0.2
done
sleep 1   # let it draw a few frames
DISPLAY="$DISPLAY_NUM" import -window root "$OUT/screenshot.png" || true
sed 's/\x1b\[[0-9;]*m//g' "$LOG" > "$OUT/app.log" 2> /dev/null || true

if ! kill -0 "$APP_PID" 2> /dev/null; then
    wait "$APP_PID" || true
    echo "FAIL: the app exited early. Log: $OUT/app.log, stdout: $OUT/stdout.txt"; exit 1
fi
if ! grep -qF "$EXPECT" "$OUT/app.log"; then
    echo "FAIL: the setup screen never got the focus. Log: $OUT/app.log"; exit 1
fi
echo "PASS: setup screen up ($(grep -m1 'app: suwayomi-nx ' "$OUT/app.log" | sed 's/^[0-9]* [A-Z] app: //')). Screenshot: $OUT/screenshot.png"
