#!/usr/bin/env bash
# Builds sys-autopilot-nx inside WSL and lays out the files for the SD card.
#
# Run from Windows:
#   wsl.exe -d Ubuntu --exec bash -lc '/mnt/c/Users/JohnDoe/Projects/suwayomi-nx/tools/sys-autopilot-nx/build.sh'
#
# Options:
#   --log     write "log = true" into the SD config.ini (for the first boots)
#   --app     also build the dev .nro flavor (runs as a normal homebrew app)
#   --no-test skip the host tests
#
# What it does, in order:
#   1. copies the source to $BUILD_DIR/src (building on /mnt/c is slow)
#   2. runs the host tests (tests/run.sh)
#   3. builds the sysmodule (and the dev .nro with --app)
#   4. prints the memory numbers (aarch64-none-elf-size) and the biggest buffers
#   5. safety gate: fails if any dangerous system call got linked in
#   6. writes the SD card layout to $BUILD_DIR/sd/, with the bearer token taken
#      from tests/e2e/device/.env (never from the command line or the docs)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SRC_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/suwayomi-nx/sys-autopilot-nx}"
ENV_FILE="$REPO_DIR/tests/e2e/device/.env"
TITLE_ID=4200000000004150

WANT_LOG=false
WANT_APP=false
WANT_TEST=true
for arg in "$@"; do
    case "$arg" in
        --log) WANT_LOG=true ;;
        --app) WANT_APP=true ;;
        --no-test) WANT_TEST=false ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [ -z "${DEVKITPRO:-}" ]; then
    echo "DEVKITPRO is not set: run through bash -l so the devkitPro profile loads" >&2
    exit 1
fi
BIN="$DEVKITPRO/devkitA64/bin"

echo "== copy source to $BUILD_DIR/src"
mkdir -p "$BUILD_DIR/src"
rsync -a --delete \
    --exclude build/ --exclude dist/ --exclude 'tests/build/' \
    --exclude '*.elf' --exclude '*.nso' --exclude '*.npdm' --exclude '*.nsp' \
    --exclude '*.nro' --exclude '*.nacp' --exclude '*.map' \
    "$SRC_DIR/" "$BUILD_DIR/src/"
cd "$BUILD_DIR/src"

if $WANT_TEST; then
    echo "== host tests"
    ./tests/run.sh
fi

echo "== build sysmodule"
make -j"$(nproc)" 2>&1 | grep -v '^\(built ...\|linking\|compiling\)' || true
test -f sys-autopilot.nsp || { echo "build failed: no sys-autopilot.nsp" >&2; exit 1; }

if $WANT_APP; then
    echo "== build dev .nro"
    make -C app -j"$(nproc)" >/dev/null
    test -f app/sys-autopilot-app.nro || { echo "build failed: no dev .nro" >&2; exit 1; }
fi

echo "== memory numbers (sysmodule ELF)"
"$BIN/aarch64-none-elf-size" sys-autopilot.elf
echo ".bss: $("$BIN/aarch64-none-elf-size" -A sys-autopilot.elf | awk '$1==".bss"{print $2}') bytes"
echo "largest buffers (bytes):"
"$BIN/aarch64-none-elf-nm" --size-sort -S -r sys-autopilot.elf |
    awk '($3=="b"||$3=="B"||$3=="d"||$3=="D") && $4!="__argdata__" {printf "  %8d  %s\n", strtonum("0x"$2), $4}' | head -8

echo "== safety gate: no dangerous system calls linked"
# The sysmodule must physically be unable to install titles, change settings,
# DNS, clocks or power state, or throw a fatal. (libnx only links functions
# that are called, so a name in the symbol table means the code calls it.)
BANNED='^(fatalThrow|fsOpenBis|fsOpenSaveDataFileSystemBySystemSaveDataId|fsCreate.*SaveData|fsDelete.*SaveData|fsFormat|setsysSet|setSet|nsDeleteApplication|ns.*Install|ncmContentStorage(WritePlaceHolder|Register|Delete)|ncmContentMetaDatabase(Set|Remove)|esImportTicket|esDeleteTicket|splSetConfig|bpcRebootSystem|bpcShutdownSystem|spsm|pcvSet|clkrstSet|apmSetPerformanceConfiguration|lblSet|audctl|nifmSetNetworkProfile|nifmSetWirelessCommunicationEnabled|timeSet)'
FOUND="$("$BIN/aarch64-none-elf-nm" sys-autopilot.elf | awk '{print $NF}' | grep -E "$BANNED" || true)"
if [ -n "$FOUND" ]; then
    echo "FAILED: banned symbols linked:" >&2
    echo "$FOUND" >&2
    exit 1
fi
echo "ok"

echo "== SD card layout -> $BUILD_DIR/sd"
if [ ! -f "$ENV_FILE" ]; then
    echo "missing $ENV_FILE (needs SWITCH_TOKEN=...)" >&2
    exit 1
fi
TOKEN="$(sed -n 's/^SWITCH_TOKEN=//p' "$ENV_FILE" | tr -d '\r')"
if [ "${#TOKEN}" -lt 32 ]; then
    echo "SWITCH_TOKEN in $ENV_FILE is missing or shorter than 32 characters" >&2
    exit 1
fi
rm -rf "$BUILD_DIR/sd"
mkdir -p "$BUILD_DIR/sd/atmosphere/contents/$TITLE_ID/flags" "$BUILD_DIR/sd/config/sys-autopilot"
cp sys-autopilot.nsp "$BUILD_DIR/sd/atmosphere/contents/$TITLE_ID/exefs.nsp"
: > "$BUILD_DIR/sd/atmosphere/contents/$TITLE_ID/flags/boot2.flag"
sed -e "s/^token =.*/token = $TOKEN/" \
    -e "s/^hostname =.*/hostname = switch-dev/" \
    -e "s/^log =.*/log = $WANT_LOG/" \
    default-config.ini > "$BUILD_DIR/sd/config/sys-autopilot/config.ini"
if $WANT_APP; then
    mkdir -p "$BUILD_DIR/sd/switch"
    cp app/sys-autopilot-app.nro "$BUILD_DIR/sd/switch/sys-autopilot-nx-dev.nro"
fi
(cd "$BUILD_DIR/sd" && find . -type f | sort | while read -r f; do
    printf '  %-58s %8d bytes  sha256 %s\n' "$f" "$(stat -c %s "$f")" "$(sha256sum "$f" | cut -c1-16)"
done)
echo "version: $(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' package.json), log = $WANT_LOG"
echo "Windows path: \\\\wsl.localhost\\Ubuntu${BUILD_DIR//\//\\}\\sd"
