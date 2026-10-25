#!/usr/bin/env bash
# Talks to sys-autopilot-nx on the test Switch with curl (run inside WSL).
# The host, port and bearer token come from tests/e2e/device/.env, and the
# token is handed to curl on stdin, so it never appears in a command line,
# a shell history or a process list.
#
# Usage:
#   sa.sh GET /status
#   sa.sh POST /input/tap '{"buttons":["A"]}'
#   sa.sh POST /input/touch '{"x":640,"y":360}'
#   sa.sh -o shot.jpg GET /screenshot
#   sa.sh PUT '/files?path=/switch/suwayomi-nx/test.txt' @local-file.txt
#   SWITCH_HOST=switch-dev.local sa.sh GET /status     # try mDNS instead of the IP
#   sa.sh --no-auth GET /status                        # check that auth is enforced
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
ENV_FILE="$REPO_DIR/tests/e2e/device/.env"

OUT=""
AUTH=true
while [ $# -gt 0 ]; do
    case "$1" in
        -o) OUT="$2"; shift 2 ;;
        --no-auth) AUTH=false; shift ;;
        *) break ;;
    esac
done
if [ $# -lt 2 ]; then
    sed -n '2,15p' "$0" >&2
    exit 2
fi
METHOD="$1"
URL_PATH="$2"
BODY="${3:-}"

env_value() { sed -n "s/^$1=//p" "$ENV_FILE" | tr -d '\r'; }
HOST="${SWITCH_HOST:-$(env_value SWITCH_HOST)}"
PORT="${SWITCH_PORT:-$(env_value SWITCH_PORT)}"
TOKEN="$(env_value SWITCH_TOKEN)"

ARGS=(-sS --max-time 60 -X "$METHOD" -K -)
if [ -n "$OUT" ]; then
    ARGS+=(-o "$OUT" -w 'HTTP %{http_code}, %{size_download} bytes, %{time_total}s\n')
else
    ARGS+=(-w '\nHTTP %{http_code}, %{time_total}s\n')
fi
case "$BODY" in
    "") ;;
    @*) ARGS+=(-T "${BODY#@}") ;;
    *) ARGS+=(-H 'Content-Type: application/json' --data-binary "$BODY") ;;
esac

# curl reads its config (the auth header) from stdin because of "-K -".
if $AUTH; then
    printf 'header = "Authorization: Bearer %s"\n' "$TOKEN"
else
    printf '\n'
fi | curl "${ARGS[@]}" "http://$HOST:$PORT$URL_PATH"
