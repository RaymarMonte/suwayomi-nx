#!/usr/bin/env bash
# Turns the "BASE + 0x…" addresses in a crash record (src/app/crash/README.md) into function names and file:line, using the
# .elf the crashed app was built from. Runs inside WSL.
#
# Usage:  tools/symbolize-crash/symbolize-crash.sh <crash-record.txt> [--elf <file>]
#   Switch records: the .elf is found by the record's build_id in ~/build/suwayomi-nx/archive/<build id>/ (tools/build.sh keeps
#                   the newest 20 there), and aarch64-none-elf-addr2line from devkitPro reads it.
#   Desktop records: the desktop builds in ~/build/suwayomi-nx/desktop*/ are searched for the same build id; the PC's addr2line.
#   --elf: use this file instead (it must be the exact build: the build ids are compared).
# Output: the record, with "  -> function at file:line" after each address it could look up. Return addresses on the stack are
# looked up one byte earlier, so the line shown is the call itself, not the line after it.
set -euo pipefail

RECORD="${1:?usage: symbolize-crash.sh <crash-record.txt> [--elf <file>]}"
shift
ELF=""
while [ $# -gt 0 ]; do
    case "$1" in
        --elf) ELF="${2:?--elf needs a file}"; shift ;;
        *) echo "unknown argument: $1"; exit 2 ;;
    esac
    shift
done
BUILD_ROOT="${SNX_BUILD_ROOT:-$HOME/build/suwayomi-nx}"

field() { sed -n "s/^$1: //p" "$RECORD" | head -1 | tr -d '\r'; }
PLATFORM=$(field platform)
BUILD_ID=$(field build_id)
head -1 "$RECORD" | grep -q '^suwayomi-nx crash record' || { echo "$RECORD is not a suwayomi-nx crash record"; exit 1; }

if [ "$PLATFORM" = switch ]; then
    TOOLS="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin"
    ADDR2LINE="$TOOLS/aarch64-none-elf-addr2line"
    READELF="$TOOLS/aarch64-none-elf-readelf"
else
    ADDR2LINE=addr2line
    READELF=readelf
fi
build_id_of() { "$READELF" -n "$1" 2> /dev/null | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p'; }

if [ -z "$ELF" ]; then
    if [ "$PLATFORM" = switch ]; then
        ELF="$BUILD_ROOT/archive/$BUILD_ID/suwayomi-nx.elf"
    else
        for candidate in "$BUILD_ROOT"/desktop*/suwayomi-nx; do
            [ "$(build_id_of "$candidate")" = "$BUILD_ID" ] && { ELF="$candidate"; break; }
        done
    fi
fi
if [ -z "$ELF" ] || [ ! -f "$ELF" ]; then
    echo "no .elf for build $BUILD_ID ($PLATFORM). Pass the right one with --elf <file>."
    exit 1
fi
if [ -n "$BUILD_ID" ] && [ "$BUILD_ID" != unknown ] && [ "$(build_id_of "$ELF")" != "$BUILD_ID" ]; then
    echo "$ELF is build $(build_id_of "$ELF"), the record is from build $BUILD_ID: the lines would be wrong."
    exit 1
fi
echo "# symbolized with $ELF"

# lookup ADDRESS: "function at file:line", inlined callers joined with " / ".
lookup() {
    "$ADDR2LINE" -f -C -i -p -e "$ELF" "$1" 2> /dev/null | sed 's/ (inlined by) / \/ /' | paste -sd' ' -
}

while IFS= read -r line; do
    line="${line%$'\r'}"
    printf '%s\n' "$line"
    if [[ "$line" =~ BASE\ \+\ 0x([0-9a-fA-F]+) ]]; then
        address=$((16#${BASH_REMATCH[1]}))
        # Stack entries and lr are return addresses: one byte back lands inside the call instruction.
        [[ "$line" =~ ^\ +(#[0-9]+|lr:) ]] && [ "$address" -gt 0 ] && address=$((address - 1))
        printf '      -> %s\n' "$(lookup "$(printf '0x%x' "$address")")"
    fi
done < "$RECORD"
