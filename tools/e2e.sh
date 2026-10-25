#!/usr/bin/env bash
# Runs E2E scenarios (docs/TESTING.md, tests/e2e/README.md) against a fresh test build. Runs inside WSL:
#   wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/e2e.sh desktop smoke'
#
# Usage:  tools/e2e.sh desktop|switch [scenario] [--update-goldens] [--visible] [--no-build]
#   scenario          file name in tests/e2e/scenarios/ without .test.ts (default: all of them)
#   --update-goldens  write the golden screenshots instead of comparing (only for intended UI changes; say so in the commit)
#   --visible         desktop: show the window on the WSLg screen instead of the hidden Xvfb one
#   --no-build        use the last test build as it is
# Builds (test hooks on): desktop -> ~/build/suwayomi-nx/desktop-test/, switch -> ~/build/suwayomi-nx/switch-test/suwayomi-nx.nro
# Output: tests/e2e/out/<scenario>/ (events, app log, failure screenshots and golden diffs), build log in tests/e2e/out/.
# The Switch needs the console on and reachable, the HOME menu's first tile = the app-mode game (docs/STATUS.md console facts).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}"

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 2; }

DRIVER="${1:-}"; [ -n "$DRIVER" ] || usage; shift
case "$DRIVER" in desktop | switch) ;; *) usage ;; esac
SCENARIO=""
UPDATE=0; VISIBLE=0; BUILD=1
while [ $# -gt 0 ]; do
    case "$1" in
        --update-goldens) UPDATE=1 ;;
        --visible) VISIBLE=1 ;;
        --no-build) BUILD=0 ;;
        -*) echo "unknown flag: $1"; usage ;;
        *) [ -z "$SCENARIO" ] || usage; SCENARIO="$1" ;;
    esac
    shift
done
if [ -n "$SCENARIO" ] && [ ! -f "$REPO/tests/e2e/scenarios/$SCENARIO.test.ts" ]; then
    echo "no scenario tests/e2e/scenarios/$SCENARIO.test.ts"; exit 2
fi

mkdir -p "$REPO/tests/e2e/out"
if [ "$DRIVER" = desktop ]; then TARGET="$BUILD_ROOT/desktop-test"; else TARGET="$BUILD_ROOT/switch-test/suwayomi-nx.nro"; fi
if [ "$BUILD" = 1 ]; then
    LOG="$REPO/tests/e2e/out/build-$DRIVER.log"
    echo "[e2e] building the $DRIVER test build (log: $LOG)"
    if ! "$REPO/tools/build.sh" "$DRIVER" --test-hooks > "$LOG" 2>&1; then
        tail -30 "$LOG"; echo "[e2e] build failed, see $LOG"; exit 1
    fi
fi

cd "$REPO"
[ -d node_modules ] || npm ci
export SNX_E2E_DRIVER="$DRIVER" SNX_E2E_BUILD="$TARGET" SNX_E2E_VISIBLE="$VISIBLE" SNX_UPDATE_GOLDENS="$UPDATE"
FILTER=()
[ -n "$SCENARIO" ] && FILTER=("tests/e2e/scenarios/$SCENARIO.test.ts")
exec npx vitest run -c vitest.e2e.config.ts "${FILTER[@]}"
