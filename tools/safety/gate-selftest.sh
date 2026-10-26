#!/usr/bin/env bash
# Proves the safety gate works (WP-03b acceptance, WP-03c): builds the Switch app once per probe with one banned call linked
# in on purpose (src/app/SafetyGateProbe.cpp) and checks that each build FAILS in tools/safety/check-elf.sh with the expected
# message and leaves no .nro behind. The normal build passing is the other half (tools/build.sh switch).
# Runs inside WSL with devkitPro; ~1-2 min. Build folder: ~/build/suwayomi-nx/switch-gate-probe/.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIR="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}/switch-gate-probe"
[ -n "${DEVKITPRO:-}" ] || { echo "DEVKITPRO is not set: use a login shell (bash -l)"; exit 1; }

# <probe>|<what it links>|<the gate's failure line it must cause>
PROBES=(
    "S2|fatalThrow|FAIL: fatalThrow is in the .elf (S2"
    "S3|setsysSetColorSetId|FAIL: setsysSetColorSetId is in the .elf (S3"
    "BOOST|a call to appletSetCpuBoostMode|FAIL: appletSetCpuBoostMode is referenced outside _appletCleanup (S3 rule appletSetCpuBoostMode, only-from): code snxSafetyGateProbe"
    "BOOSTPTR|appletSetCpuBoostMode's address in data|FAIL: appletSetCpuBoostMode is referenced outside _appletCleanup (S3 rule appletSetCpuBoostMode, only-from): data"
)

failures=0
for entry in "${PROBES[@]}"; do
    IFS='|' read -r probe what expected <<< "$entry"
    echo "[gate-selftest] $probe: linking $what on purpose, the build must fail"
    cmake -S "$REPO" -B "$DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON \
        -DSNX_SAFETY_GATE_PROBE="$probe" > "$DIR.configure.log" 2>&1 || { cat "$DIR.configure.log"; exit 1; }
    if cmake --build "$DIR" --target suwayomi-nx.nro > "$DIR.$probe.log" 2>&1; then
        echo "  WRONG: the build passed the gate with $what in it"; failures=$((failures + 1)); continue
    fi
    if ! grep -qF "$expected" "$DIR.$probe.log"; then
        echo "  WRONG: the build failed, but not with \"$expected\". Log: $DIR.$probe.log"; tail -20 "$DIR.$probe.log"
        failures=$((failures + 1)); continue
    fi
    if [ -e "$DIR/suwayomi-nx.nro" ]; then
        echo "  WRONG: a .nro was left behind"; failures=$((failures + 1)); continue
    fi
    echo "  ok: $(grep -m1 -F "$expected" "$DIR.$probe.log")"
done
[ "$failures" = 0 ] && echo "[gate-selftest] PASS: all ${#PROBES[@]} deliberately linked banned calls failed the build, no .nro made"
exit $((failures > 0))
