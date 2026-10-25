#!/usr/bin/env bash
# Re-records every response in tests/fixtures/responses/ on a fresh test server: the auth "none" queries first, then the
# ui_login ones. Some queries change server state (reading progress), hence the reset. Leaves the server running (auth none).
# Usage (WSL): tools/record-fixtures/record-all.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$REPO/tools/test-server/test-server.sh"

"$T" reset
for auth in none ui_login; do
    "$T" start --auth "$auth"
    eval "$("$T" env)"
    node "$REPO/tools/record-fixtures/record-fixtures.ts"
done
"$T" start --auth none
