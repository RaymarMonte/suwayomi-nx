#!/usr/bin/env bash
# The suwayomi-nx test server (WP-02, docs/DECISIONS.md D29): a throwaway Suwayomi-Server on Windows, port 4568, filled with
# the fixture manga. Runs in WSL. Java is started on Windows by test-server.ps1 (so the Switch can reach it, D11); this script
# adds the fixture manga and the library setup. The owner's real Suwayomi data is never touched.
#
# Usage: tools/test-server/test-server.sh <command> [--auth none|basic|ui_login]
#   reset    stop, then a fresh data folder with the fixture manga (re-generates them first when the generator changed)
#   start    start (restarts when --auth differs), wait until it answers, set up the library (idempotent). Default auth: none
#   stop     stop it
#   status   one line of JSON: running, auth, version, LAN addresses, and the URL to use from WSL
#   url      the base URL to use from WSL, e.g. http://172.27.64.1:4568
#   env      shell lines for other tools: eval "$(tools/test-server/test-server.sh env)"
# Typical: tools/test-server/test-server.sh reset && tools/test-server/test-server.sh start
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PS1_WIN="$(wslpath -w "$REPO/tools/test-server/test-server.ps1")"
CONFIG="$REPO/tools/test-server/server.json"

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 2; }

CMD="${1:-}"; [ -n "$CMD" ] || usage; shift
AUTH=none
while [ $# -gt 0 ]; do
    case "$1" in
        --auth) AUTH="${2:-}"; shift ;;
        *) echo "unknown argument: $1"; usage ;;
    esac
    shift
done
case "$AUTH" in none|basic|ui_login) ;; *) echo "--auth must be none, basic or ui_login"; exit 2 ;; esac

log() { echo "[test-server] $*"; }

# Windows PowerShell, without its CR line endings.
winps() { powershell.exe -NoProfile -File "$PS1_WIN" "$@" | tr -d '\r'; }

# WSL 2 on Windows 10 is behind NAT: Windows is the default gateway, and "localhost" here is not Windows.
windows_host() { ip route show default | awk '{ print $3; exit }'; }
base_url() { echo "http://$(windows_host):$(jq -r .port "$CONFIG")"; }

paths_json=""
win_path() {   # win_path <key>: a folder from test-server.ps1 paths, as a WSL path
    [ -n "$paths_json" ] || paths_json="$(winps paths)"
    wslpath -u "$(jq -r ".$1" <<< "$paths_json")"
}

ensure_node_modules() {
    if [ ! -d "$REPO/node_modules/sharp" ]; then
        log "installing the pinned Node packages (npm ci) ..."
        (cd "$REPO" && npm ci --no-audit --no-fund)
    fi
}

# Regenerates the pristine fixtures only when the generator, the page code or the package versions changed.
ensure_fixtures() {
    local fixtures stampfile stamp
    fixtures="$(win_path fixtures)"
    stampfile="$(win_path base)/fixtures.stamp"
    stamp="$(cd "$REPO" && find tools/gen-fixtures tests/e2e/lib/pagecode.ts package-lock.json -name '*.ts' ! -name '*.test.ts' -o -name package-lock.json \
        | sort | xargs sha256sum | sha256sum | cut -c1-64)"
    if [ -d "$fixtures/Alpha" ] && [ -f "$stampfile" ] && [ "$(cat "$stampfile")" = "$stamp" ]; then
        return
    fi
    ensure_node_modules
    log "generating fixture manga -> $fixtures"
    node "$REPO/tools/gen-fixtures/gen-fixtures.ts" --out "$fixtures"
    echo "$stamp" > "$stampfile"
}

server_env() {
    echo "export SNX_SERVER_URL='$(base_url)'"
    echo "export SNX_AUTH='$AUTH'"
    echo "export SNX_USER='$(jq -r .username "$CONFIG")'"
    echo "export SNX_PASS='$(jq -r .password "$CONFIG")'"
}

case "$CMD" in
    reset)
        ensure_fixtures
        winps reset
        ;;
    start)
        [ -f "$(win_path root)/.snx-test-root" ] || { ensure_fixtures; winps reset; }
        winps start -Auth "$AUTH"
        ensure_node_modules
        eval "$(server_env)"
        node "$REPO/tools/test-server/seed.ts"
        log "ready: $(base_url) (auth $AUTH). From the LAN: http://<pc-ip>:$(jq -r .port "$CONFIG"), see status"
        ;;
    stop)
        winps stop
        ;;
    status)
        winps status | jq -c --arg url "$(base_url)" '. + { wslUrl: $url }'
        ;;
    url)
        base_url
        ;;
    env)
        # The auth mode the server runs with now (status), not the --auth default.
        AUTH="$(winps status | jq -r '.auth // "none"')"
        server_env
        ;;
    *)
        usage
        ;;
esac
