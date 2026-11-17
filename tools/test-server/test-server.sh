#!/usr/bin/env bash
# The suwayomi-nx test server (WP-02, docs/DECISIONS.md D29): a throwaway Suwayomi-Server on Windows, port 4568, filled with
# the fixture manga. Runs in WSL. Java is started on Windows by test-server.ps1 (so the Switch can reach it, D11); this script
# adds the fixture manga, the test source (WP-23: store + site on 127.0.0.1:4569, our extension) and the library setup.
# The owner's real Suwayomi data is never touched.
#
# Usage: tools/test-server/test-server.sh <command> [--auth none|basic|ui_login]
#   reset    stop, then a fresh data folder with the fixture manga (re-generates them first when the generator changed);
#            the test source too (site, store on SNX Test v1, no switch file; jars rebuilt when the Java code changed)
#   start    start (restarts when --auth differs), wait until it answers, set up the library (idempotent). Default auth: none
#   stop     stop it
#   status   one line of JSON: running, auth, version, LAN addresses, the test site, and the URL to use from WSL
#   url      the base URL to use from WSL, e.g. http://172.27.64.1:4568
#   env      shell lines for other tools (SNX_SERVER_URL, SNX_STORE_URL, ...): eval "$(tools/test-server/test-server.sh env)"
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
    local fixtures extra stampfile stamp
    fixtures="$(win_path fixtures)"
    extra="$(win_path base)/fixtures-extra"   # Epsilon Weekly (WP-15): kept out of $fixtures, which reset copies as it is
    stampfile="$(win_path base)/fixtures.stamp"
    # The test source's files (WP-23) don't change these fixtures: they have their own stamp (ensure_test_site).
    stamp="$(cd "$REPO" && find tools/gen-fixtures tests/e2e/lib/pagecode.ts package-lock.json -name '*.ts' ! -name '*.test.ts' ! -name '*test-source*' -o -name package-lock.json \
        | sort | xargs sha256sum | sha256sum | cut -c1-64)"
    if [ -d "$fixtures/Alpha" ] && [ -d "$extra/Epsilon Weekly" ] && [ -f "$stampfile" ] && [ "$(cat "$stampfile")" = "$stamp" ]; then
        return
    fi
    ensure_node_modules
    log "generating fixture manga -> $fixtures"
    node "$REPO/tools/gen-fixtures/gen-fixtures.ts" --out "$fixtures" --extra-out "$extra"
    echo "$stamp" > "$stampfile"
}

# The test source (WP-23, D55): the store + test site in .test-server/web/ (Node writes them) and the extension jars in
# web/store/ (javac and jar exist only on Windows: test-server.ps1 build-source). Each part is redone only when its stamp
# changed: the site when the generator changed, the jars when the Java code, the manifest template, the server version or
# the site changed (regenerating the site deletes the jars).
ensure_test_site() {
    local web base site_stamp source_stamp jars wanted
    web="$(win_path web)"
    base="$(win_path base)"
    site_stamp="$(cd "$REPO" && find tools/gen-fixtures tests/e2e/lib/pagecode.ts package-lock.json -name '*.ts' ! -name '*.test.ts' -o -name package-lock.json \
        | sort | xargs sha256sum | sha256sum | cut -c1-64)"
    if ! { [ -f "$web/store/index.json" ] && [ -f "$base/web-site.stamp" ] && [ "$(cat "$base/web-site.stamp")" = "$site_stamp" ]; }; then
        ensure_node_modules
        log "generating the test source's store and site -> $web"
        node "$REPO/tools/gen-fixtures/gen-test-source.ts" --out "$web"
        echo "$site_stamp" > "$base/web-site.stamp"
        rm -f "$base/web-source.stamp"
    fi
    source_stamp="$( (cd "$REPO" && find tools/test-source -type f ! -name '*.md' | sort | xargs sha256sum; jq -r .version "$CONFIG"; echo "$site_stamp") \
        | sha256sum | cut -c1-64)"
    jars="$(find "$web/store" -maxdepth 1 -name '*.jar' | wc -l)"
    wanted="$(jq length "$web/store/build.json")"
    if [ "$jars" != "$wanted" ] || [ ! -f "$base/web-source.stamp" ] || [ "$(cat "$base/web-source.stamp")" != "$source_stamp" ]; then
        log "building the test extension jars (Windows JDK) ..."
        winps build-source
        echo "$source_stamp" > "$base/web-source.stamp"
    fi
}

store_url() { echo "http://127.0.0.1:$(jq -r .webPort "$CONFIG")/store/index.json"; }

server_env() {
    echo "export SNX_SERVER_URL='$(base_url)'"
    echo "export SNX_STORE_URL='$(store_url)'"
    echo "export SNX_AUTH='$AUTH'"
    echo "export SNX_USER='$(jq -r .username "$CONFIG")'"
    echo "export SNX_PASS='$(jq -r .password "$CONFIG")'"
}

case "$CMD" in
    reset)
        winps stop   # first: the test site's folder may be rewritten below
        ensure_fixtures
        ensure_test_site
        winps reset
        node "$REPO/tools/gen-fixtures/gen-test-source.ts" --reset-store "$(win_path web)"   # store on SNX Test v1, no switch file
        ;;
    start)
        if [ ! -f "$(win_path root)/.snx-test-root" ]; then
            ensure_fixtures; ensure_test_site; winps reset
            node "$REPO/tools/gen-fixtures/gen-test-source.ts" --reset-store "$(win_path web)"
        fi
        ensure_test_site
        winps start -Auth "$AUTH"
        ensure_node_modules
        eval "$(server_env)"
        node "$REPO/tools/test-server/seed.ts"
        log "ready: $(base_url) (auth $AUTH), test source store $(store_url). From the LAN: http://<pc-ip>:$(jq -r .port "$CONFIG"), see status"
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
