#!/usr/bin/env bash
# WP-00 spike helpers for driving the test Switch through sys-autopilot-nx (source this file, inside WSL).
# Throwaway: WP-03 turns these into the real SwitchDriver (tests/e2e/drivers/).
# Everything goes through tools/sys-autopilot-nx/scripts/sa.sh, so the token never shows up on a command line.

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SA="$REPO/tools/sys-autopilot-nx/scripts/sa.sh"
OUT="${OUT:-$REPO/tests/e2e/out/wp00}"
SWITCH_IP="$(sed -n 's/^SWITCH_HOST=//p' "$REPO/tests/e2e/device/.env" | tr -d '\r')"
mkdir -p "$OUT"

log() { echo "[$(date +%H:%M:%S)] $*"; }

# Buttons: A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT HOME ...
press() { for b in "$@"; do "$SA" POST /input/tap "{\"buttons\":[\"$b\"]}" > /dev/null; sleep 0.35; done; }
hold() { "$SA" POST /input/hold "{\"buttons\":[\"$1\"]}" > /dev/null; }
release() { "$SA" POST /input/release "{\"buttons\":[\"$1\"]}" > /dev/null; }
touch_at() { "$SA" POST /input/touch "{\"x\":$1,\"y\":$2}" > /dev/null; }
swipe() { "$SA" POST /input/swipe "{\"x1\":$1,\"y1\":$2,\"x2\":$3,\"y2\":$4,\"durationMs\":${5:-300}}" > /dev/null; }

# shot NAME: saves $OUT/NAME.jpg. Retries, because a capture during an app switch fails
# (HTTP 500, {"error":"capture failed","rc":"0xe0ce"}). Returns 1 if it never works.
shot() {
    local f="$OUT/$1.jpg" i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        if "$SA" -o "$f" GET /screenshot | grep -q '^HTTP 200' && [ "$(head -c 2 "$f" | od -An -tx1 | tr -d ' ')" = ffd8 ]; then
            return 0
        fi
        sleep 0.5
    done
    log "screenshot $1 failed"; return 1
}

# similar REF.png REGION(WxH+X+Y) NAME: crops the last shot NAME and compares it with REF.
# Prints the difference (0 = identical, 1 = completely different). Success if below 0.08.
similar() {
    local d
    d=$(convert "$OUT/$3.jpg" -crop "$2" +repage png:- | compare -metric RMSE "$1" png:- null: 2>&1 | sed -n 's/.*(\(.*\)).*/\1/p')
    echo "$d"
    awk -v d="$d" 'BEGIN { exit !(d != "" && d < 0.08) }'
}

# list_names PATH: file names in a folder on the SD card, one per line, sorted.
list_names() { "$SA" GET "/files?path=$1" | grep -o '"name":"[^"]*"' | sed 's/"name":"\(.*\)"/\1/' | sort; }

# get_file SDPATH LOCAL: downloads a file. Fails on anything but HTTP 200.
get_file() { "$SA" -o "$2" GET "/files?path=$1" | grep -q '^HTTP 200'; }

# deploy LOCAL SDPATH: upload + SHA-256 check (the PUT is not atomic, so the hash check is required).
deploy() {
    local want got
    want=$(sha256sum "$1" | cut -c1-64)
    "$SA" PUT "/files?path=$2" "@$1" | grep -q '^HTTP 201' || { log "upload failed"; return 1; }
    got=$("$SA" GET "/files/hash?path=$2" | grep -o '"hash":"[0-9a-f]*"' | cut -d'"' -f4)
    [ "$want" = "$got" ] || { log "hash mismatch: $want vs $got"; return 1; }
}
