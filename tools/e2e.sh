#!/usr/bin/env bash
# Runs E2E scenarios (docs/TESTING.md, tests/e2e/README.md) against a fresh test build. Runs inside WSL:
#   wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/e2e.sh desktop smoke'
#
# Usage:  tools/e2e.sh desktop|switch [scenario] [--update-goldens] [--visible] [--no-build]
#         tools/e2e.sh switch --restore-config
#   scenario          file name in tests/e2e/scenarios/ without .test.ts (default: all of them)
#   --update-goldens  write the golden screenshots instead of comparing (only for intended UI changes; say so in the commit)
#   --visible         desktop: show the window on the WSLg screen instead of the hidden Xvfb one
#   --no-build        use the last test build as it is
#   --restore-config  switch: only put the console's own config.json back, if a stopped run left the tests' one there
# Builds (test hooks on): desktop -> ~/build/suwayomi-nx/desktop-test/, switch -> ~/build/suwayomi-nx/switch-test/suwayomi-nx.nro
# Output: tests/e2e/out/<scenario>/ (events, app log, failure screenshots and golden diffs), build log in tests/e2e/out/.
# The Switch needs the console on and reachable, the HOME menu's first tile = the app-mode game (docs/STATUS.md console facts).
# A Switch run copies the console's own config.json to tests/e2e/out/owner-config/ first and puts it back after every scenario,
# and once more after the run, also after a failure or Ctrl+C (tests/e2e/drivers/ownerConfig.ts).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}"

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 2; }

DRIVER="${1:-}"; [ -n "$DRIVER" ] || usage; shift
case "$DRIVER" in desktop | switch) ;; *) usage ;; esac
SCENARIO=""
UPDATE=0; VISIBLE=0; BUILD=1; RESTORE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --update-goldens) UPDATE=1 ;;
        --visible) VISIBLE=1 ;;
        --no-build) BUILD=0 ;;
        --restore-config) RESTORE_ONLY=1 ;;
        -*) echo "unknown flag: $1"; usage ;;
        *) [ -z "$SCENARIO" ] || usage; SCENARIO="$1" ;;
    esac
    shift
done
if [ -n "$SCENARIO" ] && [ ! -f "$REPO/tests/e2e/scenarios/$SCENARIO.test.ts" ]; then
    echo "no scenario tests/e2e/scenarios/$SCENARIO.test.ts"; exit 2
fi
if [ "$RESTORE_ONLY" = 1 ]; then
    if [ "$DRIVER" != switch ] || [ -n "$SCENARIO" ]; then usage; fi
    cd "$REPO"
    [ -d node_modules ] || npm ci
    exec node tests/e2e/drivers/restore-config.ts
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
if [ "$DRIVER" = desktop ]; then exec npx vitest run -c vitest.e2e.config.ts "${FILTER[@]}"; fi

# Switch: whatever ends the run (a failure, Ctrl+C), the console's own config.json goes back afterwards. Every scenario puts it
# back itself; this catches a scenario that was stopped before it could.
trap 'echo "[e2e] stopped: putting the console config back first"' INT TERM
STATUS=0
npx vitest run -c vitest.e2e.config.ts "${FILTER[@]}" || STATUS=$?
if ! node tests/e2e/drivers/restore-config.ts; then [ "$STATUS" != 0 ] || STATUS=1; fi
exit "$STATUS"
