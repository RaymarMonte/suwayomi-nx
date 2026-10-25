#!/usr/bin/env bash
# Safety symbol scan of the Switch .elf (docs/SAFETY.md S2/S3). REPORT-ONLY for now: it prints what it finds and always
# exits 0. WP-03b turns it into a gate (fails the build, with tools/safety/allowed-symbols.txt for owner-approved exceptions).
#
# Usage (inside WSL, needs devkitPro):  tools/safety/check-elf.sh <suwayomi-nx.elf> [<suwayomi-nx.map>]
#
# What it reports:
#   - every function in the .elf that matches tools/safety/banned-symbols.txt, with the functions that call it
#     (from the disassembly: "bl"/"b" instructions) and, from the linker map, the object file that pulled it in;
#   - which object defines the crash hooks (__libnx_exception_handler, diagAbortWithResult). From WP-03b they must be ours.
# The linker drops unused functions (--gc-sections), so a function that shows up here is really linked in.
set -euo pipefail

ELF="${1:?usage: check-elf.sh <elf> [map]}"
MAP="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIST="$HERE/banned-symbols.txt"
BIN="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin"
NM="$BIN/aarch64-none-elf-nm"
OBJDUMP="$BIN/aarch64-none-elf-objdump"
READELF="$BIN/aarch64-none-elf-readelf"

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# All function symbols in the binary (T/t = text, W/w = weak), names only, not demangled (libnx is C).
"$NM" --defined-only "$ELF" | awk '$2 ~ /^[TtWw]$/ { print $3 }' | sort -u > "$TMP/functions.txt"
# Who calls what: "caller<TAB>callee" for every bl/b to a named function.
"$OBJDUMP" -d --no-show-raw-insn "$ELF" | awk '
    /^[0-9a-f]+ <.*>:$/ { fn = $0; sub(/^[0-9a-f]+ </, "", fn); sub(/>:$/, "", fn); next }
    $2 == "bl" || $2 == "b" { if (match($0, /<[^>+]+>/)) print fn "\t" substr($0, RSTART + 1, RLENGTH - 2) }
' | sort -u > "$TMP/calls.txt"

# defined_in SYMBOL: the object file whose ".text.SYMBOL" section the linker map shows (needs -ffunction-sections, which
# libnx, borealis and our code all use).
defined_in() {
    [ -n "$MAP" ] || { echo "(no map)"; return; }
    awk -v sec=".text.$1" '
        found { print $NF; exit }
        $1 == sec { if (NF >= 3) { print $NF; exit } else found = 1 }
    ' "$MAP"
}
# pulled_in_by SYMBOL: from the map's "Archive member included to satisfy reference by file (symbol)" part.
pulled_in_by() {
    [ -n "$MAP" ] || return 0
    awk -v sym="($1)" '
        /^Archive member included/ { inside = 1; next }
        /^(Discarded input sections|Allocating common symbols|Memory Configuration)/ { inside = 0 }
        inside && $NF == sym { print $(NF - 1) }
    ' "$MAP" | sort -u
}
demangle() { "$BIN/aarch64-none-elf-c++filt" 2> /dev/null || cat; }

BUILD_ID=$("$READELF" -n "$ELF" | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p')
echo "suwayomi-nx safety symbol report (docs/SAFETY.md S2/S3). REPORT-ONLY: WP-03b turns it into a gate."
echo "elf: $(basename "$ELF")  build id: ${BUILD_ID:-?}  functions: $(wc -l < "$TMP/functions.txt")"
echo "rules: tools/safety/banned-symbols.txt"
echo

found=0; s2=0
for rule in S2 S3; do
    echo "== $rule matches =="
    any=0
    while read -r r pattern; do
        [ "$r" = "$rule" ] || continue
        while read -r sym; do
            # shellcheck disable=SC2053  # the pattern is a glob on purpose
            [[ "$sym" == $pattern ]] || continue
            any=1; found=$((found + 1)); [ "$rule" = S2 ] && s2=$((s2 + 1))
            echo "$sym   (rule: $pattern)"
            echo "    defined in:  $(defined_in "$sym")"
            pulled_in_by "$sym" | sed 's/^/    pulled in by: /'
            callers=$(awk -F'\t' -v s="$sym" '$2 == s { print $1 }' "$TMP/calls.txt" | demangle)
            if [ -n "$callers" ]; then
                echo "$callers" | sed 's/^/    called from: /'
            else
                echo "    called from: (no direct call found: maybe only through a pointer or vtable)"
            fi
        done < "$TMP/functions.txt"
    done < <(grep -v '^\s*#' "$LIST" | grep -v '^\s*$')
    [ "$any" = 1 ] || echo "(none)"
    echo
done

echo "== crash hooks (S1/S2: from WP-03b these must be defined by our code, not libnx's weak defaults) =="
hooks_ours=0
for hook in __libnx_exception_handler diagAbortWithResult; do
    if grep -qx "$hook" "$TMP/functions.txt"; then
        where=$(defined_in "$hook")
        case "$where" in
            *libnx.a*) echo "$hook: libnx default ($where)" ;;
            *) echo "$hook: ours ($where)"; hooks_ours=$((hooks_ours + 1)) ;;
        esac
    else
        echo "$hook: not linked"
    fi
done
echo
echo "SUMMARY: banned matches=$found (S2 fatal=$s2, S3=$((found - s2))), crash hooks ours=$hooks_ours/2. Report-only, exit 0."
