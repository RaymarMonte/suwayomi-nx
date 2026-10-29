#!/usr/bin/env bash
# Makes a release (WP-12). Runs inside WSL, like tools/build.sh.
# 1. The Switch release build (tools/build.sh switch: no test hooks; the safety gate, tools/safety/check-elf.sh, runs inside it
#    and a failing .elf gets no .nro).
# 2. Checks on the .elf: the crash handler is in (SAFETY.md S1: release builds keep it), the test hooks and the crash drills
#    are out (only test builds have them).
# 3. Puts what a GitHub Release needs into ~/build/suwayomi-nx/release/v<version>/:
#      suwayomi-nx.nro                 the app (users copy it to sdmc:/switch/suwayomi-nx/)
#      suwayomi-nx-v<version>.elf.gz   the same build's .elf with its debug info: tools/symbolize-crash/symbolize-crash.sh turns
#                                      a user's crash record into function names and file:line with it (gunzip first; the
#                                      record's build id must match build-info.txt)
#      SHA256SUMS.txt, build-info.txt, safety-report.txt
# The version comes from CMakeLists.txt's project(VERSION ...). The work tree must be clean (the release is a commit), unless
# SNX_RELEASE_DIRTY=1 (a dry run).
# Usage: tools/release.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}"
log() { echo "[release] $*"; }
fail() { echo "[release] FAILED: $*"; exit 1; }

VERSION="$(sed -n 's/^project(suwayomi-nx VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "no version in CMakeLists.txt's project(...)"
if [ "${SNX_RELEASE_DIRTY:-}" != 1 ] && [ -n "$(git -C "$REPO" status --porcelain)" ]; then
    fail "the work tree has changes: commit them first (or SNX_RELEASE_DIRTY=1 for a dry run)"
fi
[ -n "${DEVKITPRO:-}" ] || fail "DEVKITPRO is not set: use a login shell (bash -l), see docs/SETUP.md A.2"

log "v$VERSION: Switch release build"
"$REPO/tools/build.sh" switch
DIR="$BUILD_ROOT/switch"
ELF="$DIR/suwayomi-nx.elf"
grep -q 'SNX_TEST_HOOKS:BOOL=OFF' "$DIR/CMakeCache.txt" || fail "$DIR was configured with test hooks"

log "checking the .elf"
NM="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm"
STRINGS="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-strings"
symbols="$("$NM" -C --defined-only "$ELF")"
grep -q ' __libnx_exception_handler$' <<< "$symbols" || fail "the crash handler (__libnx_exception_handler) is missing"
if grep -q 'snx::app::drills::(anonymous namespace)' <<< "$symbols"; then fail "crash drills are compiled in"; fi
if "$STRINGS" -a "$ELF" | grep -q 'test/events.jsonl'; then fail "test hooks are compiled in"; fi
grep -q '^SUMMARY.*PASS' "$DIR/safety-report.txt" || fail "the safety gate didn't pass: $DIR/safety-report.txt"

OUT="$BUILD_ROOT/release/v$VERSION"
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$DIR/suwayomi-nx.nro" "$DIR/safety-report.txt" "$OUT/"
gzip -9 -c "$ELF" > "$OUT/suwayomi-nx-v$VERSION.elf.gz"
id="$("$DEVKITPRO/devkitA64/bin/aarch64-none-elf-readelf" -n "$ELF" | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p')"
{
    echo "version=$VERSION"
    echo "build_id=$id"
    echo "git=$(git -C "$REPO" rev-parse HEAD)$([ -n "$(git -C "$REPO" status --porcelain)" ] && echo -dirty)"
    echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUT/build-info.txt"
(cd "$OUT" && sha256sum suwayomi-nx.nro "suwayomi-nx-v$VERSION.elf.gz" > SHA256SUMS.txt)
log "done: $OUT"
cat "$OUT/SHA256SUMS.txt" "$OUT/build-info.txt"
