#!/usr/bin/env bash
# Proves the safety gate works (WP-03b acceptance): builds the Switch app twice with one banned call linked in on purpose
# (src/app/SafetyGateProbe.cpp: fatalThrow for S2, setsysSetColorSetId for S3) and checks that each build FAILS in
# tools/safety/check-elf.sh, names the function, and leaves no .nro behind. The normal build passing is the other half
# (tools/build.sh switch). Runs inside WSL with devkitPro; ~1-2 min. Build folder: ~/build/suwayomi-nx/switch-gate-probe/.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIR="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}/switch-gate-probe"
[ -n "${DEVKITPRO:-}" ] || { echo "DEVKITPRO is not set: use a login shell (bash -l)"; exit 1; }

failures=0
for probe in S2:fatalThrow S3:setsysSetColorSetId; do
    rule="${probe%%:*}"; symbol="${probe#*:}"
    echo "[gate-selftest] $rule: linking $symbol on purpose, the build must fail"
    cmake -S "$REPO" -B "$DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON \
        -DSNX_SAFETY_GATE_PROBE="$rule" > "$DIR.configure.log" 2>&1 || { cat "$DIR.configure.log"; exit 1; }
    if cmake --build "$DIR" --target suwayomi-nx.nro > "$DIR.$rule.log" 2>&1; then
        echo "  WRONG: the build passed the gate with $symbol in it"; failures=$((failures + 1)); continue
    fi
    if ! grep -q "FAIL: $symbol is in the .elf ($rule" "$DIR.$rule.log"; then
        echo "  WRONG: the build failed, but not in the gate for $symbol. Log: $DIR.$rule.log"; tail -20 "$DIR.$rule.log"
        failures=$((failures + 1)); continue
    fi
    if [ -e "$DIR/suwayomi-nx.nro" ]; then
        echo "  WRONG: a .nro was left behind"; failures=$((failures + 1)); continue
    fi
    echo "  ok: $(grep -m1 "FAIL: $symbol" "$DIR.$rule.log")"
done
[ "$failures" = 0 ] && echo "[gate-selftest] PASS: both deliberately linked banned calls failed the build, no .nro made"
exit $((failures > 0))
