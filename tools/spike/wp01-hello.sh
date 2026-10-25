#!/usr/bin/env bash
# WP-01 device check (throwaway spike like wp00-loop.sh; WP-03 turns both into the real harness):
#   build suwayomi-nx.nro -> HOME -> hold R + A on the first tile (application mode) -> hbmenu -> netloader (Y) + nxlink
#   to sdmc:/switch/suwayomi-nx/ -> SHA-256 check -> Hello screen -> screenshot -> PLUS quits -> log read back
#   -> crash folders checked -> software closed from HOME.
# Same rules as wp00-loop.sh: every press that could start something waits for a screen check first.
# The Hello screen's reference crop (tests/e2e/device/local/hello-header.png, git-ignored) is cut from the first run's
# screenshot when it's missing; check that screenshot by eye once.
#
# Usage (inside WSL):  tools/spike/wp01-hello.sh
set -uo pipefail
OUT="$(cd "$(dirname "$0")/../.." && pwd)/tests/e2e/out/wp01"
source "$(dirname "$0")/lib.sh"

REF="$REPO/tests/e2e/device/local"
RUN_OUT="$OUT/run-$(date +%Y%m%d-%H%M%S)"
OUT="$RUN_OUT"; mkdir -p "$OUT"
NRO=~/build/suwayomi-nx/switch/suwayomi-nx.nro
NRO_SD=/switch/suwayomi-nx/suwayomi-nx.nro
LOG_SD=/switch/suwayomi-nx/logs/app.log
HELLO_REGION=520x90+40+0   # header: app icon + "Suwayomi NX" (same region as the demo's header check)
T_START=$(date +%s.%N)

fail() { log "FAILED: $*"; log "screenshots and logs: $RUN_OUT"; exit 1; }
since() { echo "$(date +%s.%N) - $1" | bc | xargs printf '%.1f'; }
step() { STEP_T=$(date +%s.%N); log "$*"; }
done_step() { log "   ok ($(since "$STEP_T") s)"; }

wait_for() {
    local end=$(( $(date +%s) + $4 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        shot "$3" > /dev/null && similar "$1" "$2" "$3" > /dev/null && return 0
        sleep 0.3
    done
    return 1
}
close_software() {
    press X
    wait_for "$REF/close-dialog.png" 420x60+530+285 close-dialog 4 || return 1
    press A; sleep 2.5
}
crash_lists() { list_names /atmosphere/crash_reports > "$OUT/crash_reports-$1.txt"; list_names /atmosphere/fatal_errors > "$OUT/fatal_errors-$1.txt"; }

step "1/9 build (tools/build.sh switch)"
"$REPO/tools/build.sh" switch > "$OUT/build.txt" 2>&1 || fail "build, see $OUT/build.txt"
done_step

step "2/9 console reachable + crash folders before"
"$SA" GET /status | grep -q '^HTTP 200' || fail "sys-autopilot-nx does not answer (console asleep? airplane mode?)"
crash_lists before
"$SA" DELETE "/files?path=$LOG_SD" > /dev/null   # so the log we read later is from this run
done_step

step "3/9 HOME menu, first tile selected, nothing running"
press HOME; sleep 1.5; shot home > /dev/null
if ! similar "$REF/home-first-tile.png" 340x320+40+140 home > /dev/null; then
    close_software || fail "HOME menu is not on the first tile, and nothing to close"
    wait_for "$REF/home-first-tile.png" 340x320+40+140 home 5 || fail "HOME menu is not on the first tile (Spelunky 2)"
fi
done_step

step "4/9 hold R + A on the first tile + A on the user picker -> hbmenu (application mode)"
hold R; sleep 0.3; press A
if ! wait_for "$REF/user-picker.png" 520x50+50+345 picker 8; then release R; fail "user picker did not appear"; fi
press A
if ! wait_for "$REF/hbmenu-header.png" 260x100+30+20 hbmenu 15; then release R; fail "hbmenu did not appear"; fi
release R
similar "$REF/hbmenu-applet-badge.png" 340x60+700+18 hbmenu > /dev/null && fail "hbmenu says Applet Mode"
done_step

step "5/9 launch: netloader (Y) + nxlink -p suwayomi-nx/suwayomi-nx.nro, args -d --log-file"
press Y; sleep 1.5
ok=false
for i in 1 2 3 4 5; do
    if nxlink -a "$SWITCH_IP" -p suwayomi-nx/suwayomi-nx.nro --args "-d --log-file" "$NRO" > "$OUT/nxlink.txt" 2>&1; then ok=true; break; fi
    sleep 1
done
$ok || fail "nxlink, see $OUT/nxlink.txt"
done_step

step "6/9 Hello screen + screenshot"
if [ -f "$REF/hello-header.png" ]; then
    wait_for "$REF/hello-header.png" "$HELLO_REGION" hello 15 || fail "Hello screen did not appear"
else
    # First run: wait until hbmenu is gone, give the app a moment, then cut the reference from this screenshot.
    end=$(( $(date +%s) + 15 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        shot hello > /dev/null && ! similar "$REF/hbmenu-header.png" 260x100+30+20 hello > /dev/null && break
        sleep 0.3
    done
    sleep 3; shot hello > /dev/null || fail "no screenshot of the Hello screen"
    convert "$OUT/hello.jpg" -crop "$HELLO_REGION" +repage "$REF/hello-header.png"
    log "   NEW reference crop $REF/hello-header.png: check $OUT/hello.jpg by eye once"
fi
log "   screenshot: $OUT/hello.jpg"
done_step

step "7/9 .nro on the card matches the build (SHA-256)"
want=$(sha256sum "$NRO" | cut -c1-64)
got=$("$SA" GET "/files/hash?path=$NRO_SD" | grep -o '"hash":"[0-9a-f]*"' | cut -d'"' -f4)
[ "$want" = "$got" ] || fail "hash mismatch: $want vs $got"
done_step

step "8/9 PLUS quits -> back in hbmenu, log read back"
press PLUS
wait_for "$REF/hbmenu-header.png" 260x100+30+20 hbmenu-after 10 || fail "not back in hbmenu after PLUS"
get_file "$LOG_SD" "$OUT/app.log" || fail "could not read $LOG_SD"
sed -i 's/\x1b\[[0-9;]*m//g' "$OUT/app.log"
grep -q 'suwayomi-nx 0\.' "$OUT/app.log" || fail "log has no 'suwayomi-nx <version>' line"
grep -qF 'Giving focus to brls::Box (id="hello/box")' "$OUT/app.log" || fail "log: the Hello screen never got the focus"
grep -q 'Exiting\.\.\.' "$OUT/app.log" || fail "log has no 'Exiting...' line"
log "   log: $(wc -l < "$OUT/app.log") lines, first: $(grep -m1 'suwayomi-nx 0\.' "$OUT/app.log" | sed 's/.*\] //')"
done_step

step "9/9 crash folders after + close the software (back to the start state)"
crash_lists after
NEW_FATAL=$(comm -13 "$OUT/fatal_errors-before.txt" "$OUT/fatal_errors-after.txt")
[ -z "$NEW_FATAL" ] || fail "NEW FATAL ERROR FILE: $NEW_FATAL. Stop and tell the owner."
NEW_CRASH=$(comm -13 "$OUT/crash_reports-before.txt" "$OUT/crash_reports-after.txt")
[ -z "$NEW_CRASH" ] || fail "new crash report: $NEW_CRASH"
press HOME; sleep 1.5
close_software || fail "no Close Software dialog"
wait_for "$REF/home-first-tile.png" 340x320+40+140 home-end 6 || fail "software not closed / HOME not on the first tile"
done_step

log "PASS in $(since "$T_START") s. Output: $RUN_OUT"
