#!/usr/bin/env bash
# WP-00 acceptance loop (throwaway spike; WP-03 turns it into the real harness):
#   build -> deploy (PUT + SHA-256) -> HOME -> hold R + A on the first tile (application mode) -> hbmenu
#   -> netloader (Y) + nxlink -> 3 button presses -> 1 touch tap -> screenshot -> PLUS quits
#   -> log file read back -> crash folders checked -> software closed from HOME.
# Needs no hands on the console: the HOME menu's first tile must be the app-mode title (Spelunky 2), and the
# reference crops in tests/e2e/device/local/ must exist (made in WP-00 from this console's screenshots).
# Every screen is checked against a reference crop before the next press; any mismatch stops the run.
#
# Usage (inside WSL):  tools/spike/wp00-loop.sh
set -uo pipefail
source "$(dirname "$0")/lib.sh"

REF="$REPO/tests/e2e/device/local"
RUN_OUT="$OUT/run-$(date +%Y%m%d-%H%M%S)"
OUT="$RUN_OUT"; mkdir -p "$OUT"
NRO=~/build/suwayomi-nx/spike/borealis-build/switch/borealis_demo.nro
LOG_SD=/switch/suwayomi-nx/wp00/demo.log
T_START=$(date +%s.%N)

fail() { log "FAILED: $*"; log "screenshots and logs: $RUN_OUT"; exit 1; }
since() { echo "$(date +%s.%N) - $1" | bc | xargs printf '%.1f'; }
step() { STEP_T=$(date +%s.%N); log "$*"; }
done_step() { log "   ok ($(since "$STEP_T") s)"; }

# wait_for REF.png REGION NAME SECONDS: polls screenshots until the region matches the reference.
wait_for() {
    local end=$(( $(date +%s) + $4 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        shot "$3" > /dev/null && similar "$1" "$2" "$3" > /dev/null && return 0
        sleep 0.3
    done
    return 1
}

# close_software: on the HOME menu with something running, X opens "Close the software...?" with Close
# preselected. A is pressed only when that dialog is really there (otherwise A would start a game).
close_software() {
    press X
    wait_for "$REF/close-dialog.png" 420x60+530+285 close-dialog 4 || return 1
    press A; sleep 2.5
}

# Crash folders: names before, so we can spot new files after.
crash_lists() { list_names /atmosphere/crash_reports > "$OUT/crash_reports-$1.txt"; list_names /atmosphere/fatal_errors > "$OUT/fatal_errors-$1.txt"; }

step "1/9 build (Switch demo, incremental)"
"$REPO/tools/spike/build-demo.sh" switch > "$OUT/build.txt" 2>&1 || fail "build, see $OUT/build.txt"
done_step

step "2/9 console reachable + crash folders before"
"$SA" GET /status | grep -q '^HTTP 200' || fail "sys-autopilot-nx does not answer (console asleep? airplane mode?)"
crash_lists before
done_step

step "3/9 deploy: PUT /switch/borealis_demo.nro + SHA-256 check"
deploy "$NRO" /switch/borealis_demo.nro || fail "deploy"
"$SA" DELETE "/files?path=$LOG_SD" > /dev/null   # so the log we read later is from this run
done_step

step "4/9 HOME menu, first tile selected, nothing running"
press HOME; sleep 1.5; shot home > /dev/null
if ! similar "$REF/home-first-tile.png" 340x320+40+140 home > /dev/null; then
    # Something is still running (the tile shows "Playing"): close it.
    close_software || fail "HOME menu is not on the first tile, and nothing to close"
    wait_for "$REF/home-first-tile.png" 340x320+40+140 home 5 || fail "HOME menu is not on the first tile (Spelunky 2)"
fi
done_step

step "5/9 hold R + A on the first tile + A on the user picker -> hbmenu (application mode)"
hold R; sleep 0.3; press A
if ! wait_for "$REF/user-picker.png" 520x50+50+345 picker 8; then release R; fail "user picker did not appear"; fi
press A
if ! wait_for "$REF/hbmenu-header.png" 260x100+30+20 hbmenu 15; then release R; fail "hbmenu did not appear"; fi
release R
similar "$REF/hbmenu-applet-badge.png" 340x60+700+18 hbmenu > /dev/null && fail "hbmenu says Applet Mode"
done_step

step "6/9 launch: netloader (Y) + nxlink, log to sdmc:$LOG_SD"
press Y; sleep 1.5
ok=false
for i in 1 2 3 4 5; do
    if nxlink -a "$SWITCH_IP" --args "-d -o sdmc:$LOG_SD" "$NRO" > "$OUT/nxlink.txt" 2>&1; then ok=true; break; fi
    sleep 1
done
$ok || fail "nxlink, see $OUT/nxlink.txt"
wait_for "$REF/demo-header.png" 520x90+40+0 demo-start 15 || fail "demo did not appear"
done_step

step "7/9 3 button presses (DOWN x3) + 1 touch tap (Pokedex) + screenshot"
press DOWN DOWN DOWN
touch_at 155 534
wait_for "$REF/demo-pokedex.png" 600x150+460+100 demo-pokedex 5 || fail "tap on Pokedex did not open the list"
log "   screenshot: $OUT/demo-pokedex.jpg"
done_step

step "8/9 PLUS quits -> back in hbmenu, log read back"
press PLUS
wait_for "$REF/hbmenu-header.png" 260x100+30+20 hbmenu-after 10 || fail "not back in hbmenu after PLUS"
get_file "$LOG_SD" "$OUT/demo.log" || fail "could not read $LOG_SD"
sed -i 's/\x1b\[[0-9;]*m//g' "$OUT/demo.log"
grep -q 'Exiting\.\.\.' "$OUT/demo.log" || fail "log has no 'Exiting...' line"
log "   log: $(wc -l < "$OUT/demo.log") lines, $(grep -c 'Giving focus' "$OUT/demo.log") focus changes, ends with: $(tail -2 "$OUT/demo.log" | head -1)"
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
