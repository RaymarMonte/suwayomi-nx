#!/usr/bin/env bash
# Builds suwayomi-nx. Runs inside WSL (from Windows: wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/build.sh tests').
# Build folders live in ~/build/suwayomi-nx/<name>/: the Linux disk is much faster than /mnt/c.
#
# Usage:  tools/build.sh <what> [--test-hooks] [--debug]
#   tests     host unit tests, then runs them (ctest), then the GraphQL contract test (npm run contract; needs npm ci once,
#             SNX_SKIP_CONTRACT=1 skips it)                     -> ~/build/suwayomi-nx/tests/
#   coverage  unit tests with gcov, then a gcovr HTML report    -> tests/out/coverage/index.html (in the repo, git-ignored)
#   fuzz [s]  fuzz targets with ASan/UBSan (SAFETY.md S6), s seconds each (default 60). Needs clang for libFuzzer
#             (sudo apt install clang libclang-rt-18-dev); without clang it only replays the seeds under GCC's sanitizers.
#             New corpus -> ~/build/suwayomi-nx/fuzz*/corpus/, crash files -> tests/out/fuzz/ (git-ignored)
#   desktop   desktop app                                       -> ~/build/suwayomi-nx/desktop/suwayomi-nx
#   run       desktop build, then starts it (WSLg window). Extra args after "--" go to the app, e.g. run -- -d --log-file
#   switch    Switch app: .elf + .nro (only if the safety gate passes), archived by build id
#                                                               -> ~/build/suwayomi-nx/switch/suwayomi-nx.nro
#   all       tests, desktop and switch
# Flags:
#   --test-hooks  test build (SNX_TEST_HOOKS=ON, docs/TESTING.md). Uses folders named <what>-test.
#   --debug       CMAKE_BUILD_TYPE=Debug (default: Release)
# Environment: SNX_BUILD_ROOT (default ~/build/suwayomi-nx), SNX_JOBS (default: all CPUs),
#              SNX_ARCHIVE_KEEP (Switch builds kept in ~/build/suwayomi-nx/archive/, default 20).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}"
JOBS="${SNX_JOBS:-$(nproc)}"

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 2; }

WHAT="${1:-}"; [ -n "$WHAT" ] || usage; shift
TEST_HOOKS=OFF
BUILD_TYPE=Release
APP_ARGS=()
FUZZ_SECONDS=60
while [ $# -gt 0 ]; do
    case "$1" in
        [0-9]*) [ "$WHAT" = fuzz ] || { echo "a number only goes with fuzz"; usage; }; FUZZ_SECONDS="$1" ;;
        --test-hooks) TEST_HOOKS=ON ;;
        --debug) BUILD_TYPE=Debug ;;
        --) shift; APP_ARGS=("$@"); break ;;
        *) echo "unknown flag: $1"; usage ;;
    esac
    shift
done
SUFFIX=""; [ "$TEST_HOOKS" = ON ] && SUFFIX="-test"

COMMON=(-G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DSNX_TEST_HOOKS="$TEST_HOOKS")
if command -v ccache > /dev/null; then
    COMMON+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

log() { echo "[build] $*"; }

# configure DIR ARGS...: runs cmake's configure step only when the folder is new (cmake re-runs itself when needed).
configure() {
    local dir="$1"; shift
    if [ ! -f "$dir/build.ninja" ]; then
        cmake -S "$REPO" -B "$dir" "${COMMON[@]}" "$@"
    fi
}

check_borealis() {
    if [ ! -f "$REPO/library/borealis/library/lib/extern/glfw/CMakeLists.txt" ]; then
        echo "borealis is missing. Run in the repo:"
        echo "  git submodule update --init library/borealis"
        echo "  git -C library/borealis submodule update --init library/lib/extern/glfw"
        exit 1
    fi
}

build_tests() {
    local dir="$BUILD_ROOT/tests$SUFFIX"
    log "unit tests -> $dir"
    # The write-jail grep gate (SAFETY.md S4) first: it takes a second and fails before anything is built.
    "$REPO/tools/safety/check-writes.sh" --self-test
    "$REPO/tools/safety/check-writes.sh"
    configure "$dir" -DSNX_BUILD_TESTS=ON
    cmake --build "$dir" -j "$JOBS"
    ctest --test-dir "$dir" --output-on-failure
    run_contract
}

# The GraphQL contract test (TESTING.md layer 2, WP-04): our .graphql documents against the server's schema. Node tooling, so it
# needs `npm ci` once. CI's unit-test job skips it (SNX_SKIP_CONTRACT=1) because the Node job runs it.
run_contract() {
    if [ "${SNX_SKIP_CONTRACT:-}" = 1 ]; then
        log "contract test skipped (SNX_SKIP_CONTRACT=1)"
        return
    fi
    if [ ! -d "$REPO/node_modules/graphql" ]; then
        echo "The contract test needs the Node packages. Run once in the repo: npm ci   (or set SNX_SKIP_CONTRACT=1)"
        exit 1
    fi
    log "GraphQL contract test"
    (cd "$REPO" && node tests/contract/graphql-contract.ts | tail -n 1)
}

# Fuzz targets (SAFETY.md S6, tests/fuzz/). With clang: libFuzzer runs each target for FUZZ_SECONDS, starting from the seeds.
# Without clang: GCC builds the same targets with a main() that replays the seeds once (no fuzzing, but ASan/UBSan check them).
build_fuzz() {
    local cc=gcc cxx=g++ kind=replay
    if command -v clang > /dev/null && command -v clang++ > /dev/null; then
        cc=clang cxx=clang++ kind=libfuzzer
    fi
    local dir="$BUILD_ROOT/fuzz-$kind" out="$REPO/tests/out/fuzz"
    log "fuzz targets ($kind) -> $dir"
    if [ ! -f "$dir/build.ninja" ]; then
        CC=$cc CXX=$cxx cmake -S "$REPO" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSNX_FUZZ=ON
    fi
    cmake --build "$dir" -j "$JOBS" --target snx_fuzz
    mkdir -p "$out"
    local target seeds
    for target in fuzz_gql_response fuzz_app_config; do
        case "$target" in
            fuzz_gql_response) seeds="$REPO/tests/fixtures/responses" ;;
            fuzz_app_config) seeds="$REPO/tests/fuzz/seeds/app-config" ;;
        esac
        if [ "$kind" = libfuzzer ]; then
            log "$target: ${FUZZ_SECONDS}s"
            mkdir -p "$dir/corpus/$target"
            # The first folder collects new inputs, the second only gives seeds. A crash is written to tests/out/fuzz/.
            "$dir/tests/fuzz/$target" -max_total_time="$FUZZ_SECONDS" -max_len=65536 -timeout=10 -rss_limit_mb=2048 \
                -print_final_stats=1 -artifact_prefix="$out/$target-" "$dir/corpus/$target" "$seeds"
        else
            log "$target: replaying the seeds (no clang, so no libFuzzer: install clang to really fuzz)"
            "$dir/tests/fuzz/$target" "$seeds"
        fi
    done
}

build_coverage() {
    local dir="$BUILD_ROOT/coverage$SUFFIX" out="$REPO/tests/out/coverage"
    log "coverage -> $out/index.html"
    configure "$dir" -DSNX_BUILD_TESTS=ON -DSNX_COVERAGE=ON
    cmake --build "$dir" -j "$JOBS"
    find "$dir" -name '*.gcda' -delete   # counts from earlier runs would add up otherwise
    ctest --test-dir "$dir" --output-on-failure
    rm -rf "$out"; mkdir -p "$out"
    # Only our modules count (TESTING.md targets: core >= 80%, net/api/image >= 60%). Not tests/ or third_party/.
    gcovr --root "$REPO" --filter "$REPO/src/" "$dir" \
        --html-details "$out/index.html" --txt "$out/summary.txt" --print-summary
}

build_desktop() {
    check_borealis
    local dir="$BUILD_ROOT/desktop$SUFFIX"
    log "desktop app -> $dir/suwayomi-nx"
    configure "$dir" -DPLATFORM_DESKTOP=ON
    cmake --build "$dir" -j "$JOBS"
    log "run it with: cd $dir && ./suwayomi-nx   (or: tools/build.sh run)"
}

run_desktop() {
    build_desktop
    local dir="$BUILD_ROOT/desktop$SUFFIX"
    cd "$dir"
    exec ./suwayomi-nx "${APP_ARGS[@]}"
}

build_switch() {
    check_borealis
    [ -n "${DEVKITPRO:-}" ] || { echo "DEVKITPRO is not set: use a login shell (bash -l), see docs/SETUP.md A.2"; exit 1; }
    local dir="$BUILD_ROOT/switch$SUFFIX"
    log "Switch app -> $dir/suwayomi-nx.nro"
    configure "$dir" -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON
    cmake --build "$dir" -j "$JOBS" --target suwayomi-nx.nro
    archive_switch "$dir"
    # The safety gate (tools/safety/check-elf.sh) already ran inside the build, before elf2nro: a failing .elf has no .nro.
    log "safety gate report: $dir/safety-report.txt"
    grep '^SUMMARY' "$dir/safety-report.txt" || true
}

# Keeps every .nro together with the .elf it came from (the .elf has the debug info), so a crash log from any build can be
# symbolized later. Folder name = the ELF's GNU build id, which the crash handler (WP-03b) will log. Same build = same folder.
archive_switch() {
    local dir="$1" readelf="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-readelf" id dest
    id=$("$readelf" -n "$dir/suwayomi-nx.elf" | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p')
    [ -n "$id" ] || { echo "no GNU build id in $dir/suwayomi-nx.elf"; exit 1; }
    dest="$BUILD_ROOT/archive/$id"
    if [ ! -d "$dest" ]; then
        mkdir -p "$dest"
        cp "$dir/suwayomi-nx.elf" "$dir/suwayomi-nx.nro" "$dir/suwayomi-nx.map" "$dest/"
        {
            echo "build_id=$id"
            echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
            echo "git=$(git -C "$REPO" describe --always --dirty 2>/dev/null || echo unknown)"
            echo "flavor=$([ "$TEST_HOOKS" = ON ] && echo test || echo release) build_type=$BUILD_TYPE"
            echo "nro_sha256=$(sha256sum "$dir/suwayomi-nx.nro" | cut -c1-64)"
        } > "$dest/build-info.txt"
    fi
    log "archived .elf + .nro: $dest"
    # Each build is ~55 MB (the .elf's debug info). Keep the newest SNX_ARCHIVE_KEEP builds (default 20).
    ls -1dt "$BUILD_ROOT/archive"/*/ | tail -n +$(( ${SNX_ARCHIVE_KEEP:-20} + 1 )) | xargs -r rm -rf
}

case "$WHAT" in
    tests) build_tests ;;
    coverage) build_coverage ;;
    fuzz) build_fuzz ;;
    desktop) build_desktop ;;
    run) run_desktop ;;
    switch) build_switch ;;
    all) build_tests; build_desktop; build_switch ;;
    *) usage ;;
esac
