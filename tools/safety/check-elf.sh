#!/usr/bin/env bash
# The safety gate on the Switch .elf (docs/SAFETY.md S2/S3, D13, D28). Since WP-03b it FAILS the build (exit 1) when:
#   - the .elf contains a function from tools/safety/banned-symbols.txt: an S2 one ever (fatalThrow*), an S3 one unless
#     tools/safety/allowed-symbols.txt lists it (owner's OK for each entry; it is empty, D28 uses stand-ins instead);
#   - an S3 rule with "only-from=<function>" (D34): the function is referred to from anywhere but that one caller
#     (a call or jump, its address built in code, or its address stored in data);
#   - our crash hooks aren't the ones linked: __libnx_exception_handler and diagAbortWithResult must come from our code, not
#     libnx's weak default (or be missing), and __nx_exception_stack must be ours and at least 32 KB (S1).
# The Switch build runs it before elf2nro (src/app/CMakeLists.txt), so a failing .elf never becomes a .nro.
# tools/safety/gate-selftest.sh proves it fails on a deliberately linked banned call.
#
# Usage (inside WSL, needs devkitPro):  tools/safety/check-elf.sh <suwayomi-nx.elf> [<suwayomi-nx.map>] [--report <file>]
#   --report <file>  the full report goes to <file>, and only the verdict (plus any failures) to stdout.
#
# What the report shows:
#   - every function in the .elf that matches banned-symbols.txt, with the functions that call it (from the disassembly:
#     "bl"/"b" instructions) and, from the linker map, the object file that pulled it in;
#   - which object defines each crash hook;
#   - the D28 stand-ins (tools/safety/stand-ins.txt) and whether anything still calls them.
# The linker drops unused functions (--gc-sections), so a function that shows up here is really linked in.
set -euo pipefail

ELF="${1:?usage: check-elf.sh <elf> [map] [--report <file>]}"
shift
MAP=""
REPORT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --report) REPORT="${2:?--report needs a file}"; shift ;;
        *) [ -z "$MAP" ] || { echo "check-elf.sh: unexpected argument $1"; exit 2; }; MAP="$1" ;;
    esac
    shift
done
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIST="$HERE/banned-symbols.txt"
ALLOWED="$HERE/allowed-symbols.txt"
STAND_INS="$HERE/stand-ins.txt"
BIN="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin"
NM="$BIN/aarch64-none-elf-nm"
OBJDUMP="$BIN/aarch64-none-elf-objdump"
READELF="$BIN/aarch64-none-elf-readelf"
MIN_EXCEPTION_STACK=$((0x8000))

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/report.txt"
FAILS="$TMP/failures.txt"
: > "$FAILS"
fail() { echo "FAIL: $*" >> "$FAILS"; }

# All function symbols in the binary (T/t = text, W/w = weak), names only, not demangled (libnx is C).
"$NM" --defined-only "$ELF" > "$TMP/nm.txt"
awk '$2 ~ /^[TtWw]$/ { print $3 }' "$TMP/nm.txt" | sort -u > "$TMP/functions.txt"
# Data symbols with their sizes: "name size" (nm -S prints value, size, type, name).
"$NM" -S --defined-only "$ELF" | awk 'NF == 4 { print $4, $2 }' | sort -u > "$TMP/sized.txt"
# Who calls what: "caller<TAB>callee" for every bl/b to a named function.
"$OBJDUMP" -d --no-show-raw-insn "$ELF" > "$TMP/disasm.txt"
awk '
    /^[0-9a-f]+ <.*>:$/ { fn = $0; sub(/^[0-9a-f]+ </, "", fn); sub(/>:$/, "", fn); next }
    $2 == "bl" || $2 == "b" { if (match($0, /<[^>+]+>/)) print fn "\t" substr($0, RSTART + 1, RLENGTH - 2) }
' "$TMP/disasm.txt" | sort -u > "$TMP/calls.txt"
# Names in a list file: comments and empty lines dropped, first word only.
names_in() { [ -f "$1" ] && grep -v '^\s*#' "$1" | awk 'NF { print $1 }' || true; }
names_in "$ALLOWED" > "$TMP/allowed.txt"

# defined_in SYMBOL: the object file whose ".text.SYMBOL" (or .bss./.data.) section the linker map shows (needs
# -ffunction-sections / -fdata-sections, which libnx, borealis and our code all use). Only the part of the map after
# "Linker script and memory map" counts: before it, "Discarded input sections" lists libnx's unused weak defaults too.
defined_in() {
    [ -n "$MAP" ] || { echo "(no map)"; return; }
    awk -v text=".text.$1" -v bss=".bss.$1" -v data=".data.$1" '
        /^Linker script and memory map/ { linked = 1; next }
        !linked { next }
        found { print $NF; exit }
        $1 == text || $1 == bss || $1 == data { if (NF >= 3) { print $NF; exit } else found = 1 }
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

# le32 VALUE: the 32-bit value as objdump -s prints its 4 bytes in memory (little endian), e.g. 0x149cb0 -> b09c1400.
le32() { printf '%08x' "$1" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/'; }
# references SYMBOL: every place in the .elf that refers to the function SYMBOL from outside it, one per line:
#   "code<TAB><function>"            a call, jump or address with SYMBOL's name in that function (bl, b, adr, ...)
#   "address<TAB><function>"         SYMBOL's address put together with adrp + add in that function
#   "data<TAB><section> 0x<address>" SYMBOL's address stored in data (a function pointer, a GOT entry)
# For the only-from rule (banned-symbols.txt). The data search compares 8-byte-aligned words: pointers always are.
references() {
    local sym="$1" hex value page off sections
    hex=$(awk -v s="$sym" '$3 == s { print $1; exit }' "$TMP/nm.txt")
    [ -n "$hex" ] || return 0
    value=$((16#$hex))
    page=$(printf '%x' $((value & ~0xfff)))
    off=$(printf '#0x%x' $((value & 0xfff)))
    awk -v sym="$sym" -v page="$page" -v off="$off" '
        /^[0-9a-f]+ <.*>:$/ { fn = $0; sub(/^[0-9a-f]+ </, "", fn); sub(/>:$/, "", fn); delete reg; next }
        fn == sym { next }
        index($0, "<" sym ">") { print "code\t" fn }
        $2 == "adrp" { reg[$3] = $4 }
        $2 == "add" && $5 == off && reg[$4] == page { print "address\t" fn }
    ' "$TMP/disasm.txt" | sort -u
    # Sections that are loaded but not code (flag A without X): .rodata, .data, .data.rel.ro, .got, .init_array, ...
    sections=$("$READELF" -SW "$ELF" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$7 ~ /A/ && $7 !~ /X/ { printf " -j %s", $1 }')
    [ -n "$sections" ] || return 0
    # shellcheck disable=SC2086  # $sections is a list of -j flags on purpose
    "$OBJDUMP" -s $sections "$ELF" | awk -v lo="$(le32 $((value & 0xffffffff)))" -v hi="$(le32 $((value >> 32)))" '
        /^Contents of section / { sec = $4; sub(/:$/, "", sec); next }
        ($2 == lo && $3 == hi) || ($4 == lo && $5 == hi) { print "data\t" sec " 0x" $1 }
    '
}

BUILD_ID=$("$READELF" -n "$ELF" | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p')
{
echo "suwayomi-nx safety gate report (docs/SAFETY.md S2/S3, D28)."
echo "elf: $(basename "$ELF")  build id: ${BUILD_ID:-?}  functions: $(wc -l < "$TMP/functions.txt")"
echo "rules: tools/safety/banned-symbols.txt, exceptions: tools/safety/allowed-symbols.txt ($(wc -l < "$TMP/allowed.txt") entries)"
echo

found=0; s2=0; allowed=0; checked=0
for rule in S2 S3; do
    echo "== $rule matches =="
    any=0
    while read -r r pattern only; do
        [ "$r" = "$rule" ] || continue
        while read -r sym; do
            # shellcheck disable=SC2053  # the pattern is a glob on purpose
            [[ "$sym" == $pattern ]] || continue
            any=1; found=$((found + 1))
            verdict="BANNED"
            if [ "$rule" = S2 ]; then
                s2=$((s2 + 1)); fail "$sym is in the .elf (S2: fatal calls are never allowed)"
            elif [[ "$only" == only-from=* ]]; then
                # The rule's one allowed caller (owner's OK per rule, banned-symbols.txt): any other reference fails.
                ok_from="${only#only-from=}"
                refs=$(references "$sym")
                others=$(printf '%s\n' "$refs" | awk -F'\t' -v ok="$ok_from" 'NF && !($1 == "code" && $2 == ok)')
                if [ -z "$others" ]; then
                    verdict="allowed only from $ok_from: no other reference"; checked=$((checked + 1))
                else
                    fail "$sym is referenced outside $ok_from (S3 rule $pattern, only-from): $(printf '%s' "$others" | tr '\t\n' ' ;')"
                fi
            elif grep -qx "$sym" "$TMP/allowed.txt"; then
                verdict="allowed (allowed-symbols.txt)"; allowed=$((allowed + 1))
            else
                fail "$sym is in the .elf (S3 rule $pattern). See the report for who pulls it in"
            fi
            echo "$sym   (rule: $pattern${only:+ $only})   $verdict"
            echo "    defined in:  $(defined_in "$sym")"
            pulled_in_by "$sym" | sed 's/^/    pulled in by: /'
            if [[ "$only" == only-from=* ]]; then
                printf '%s\n' "$refs" | awk -F'\t' 'NF { print "    referenced: " $1 " in " $2 }' | demangle
            fi
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

echo "== crash hooks (S1/S2, D13: they must be ours, not libnx's weak defaults) =="
hooks_ours=0
for hook in __libnx_exception_handler diagAbortWithResult; do
    if grep -qx "$hook" "$TMP/functions.txt"; then
        where=$(defined_in "$hook")
        case "$where" in
            *libnx.a*) echo "$hook: libnx default ($where)"; fail "$hook is libnx's default, not ours" ;;
            *) echo "$hook: ours ($where)"; hooks_ours=$((hooks_ours + 1)) ;;
        esac
    else
        echo "$hook: not linked"; fail "$hook is not linked (our crash handler is missing)"
    fi
done
stack_size=$(awk '$1 == "__nx_exception_stack" { print $2 }' "$TMP/sized.txt")
stack_where=$(defined_in __nx_exception_stack)
if [ -z "$stack_size" ]; then
    echo "__nx_exception_stack: not linked"; fail "__nx_exception_stack is not linked"
elif [ $((16#$stack_size)) -lt $MIN_EXCEPTION_STACK ] || [[ "$stack_where" == *libnx.a* ]]; then
    echo "__nx_exception_stack: $((16#$stack_size)) bytes ($stack_where)"
    fail "__nx_exception_stack must be ours and at least $MIN_EXCEPTION_STACK bytes (it is $((16#$stack_size)), $stack_where)"
else
    echo "__nx_exception_stack: ours, $((16#$stack_size)) bytes ($stack_where)"
fi
echo

echo "== D28 stand-ins (tools/safety/stand-ins.txt) =="
any=0
while read -r name; do
    any=1
    if grep -qx "__wrap_$name" "$TMP/functions.txt"; then
        callers=$(awk -F'\t' -v s="__wrap_$name" '$2 == s { print $1 }' "$TMP/calls.txt" | demangle | paste -sd, -)
        echo "$name -> __wrap_$name: in use (called from: ${callers:-a pointer or vtable})"
    else
        echo "$name -> __wrap_$name: not linked (nothing calls $name any more; the stand-in could go)"
    fi
done < <(names_in "$STAND_INS")
[ "$any" = 1 ] || echo "(none)"
echo

nfail=$(wc -l < "$FAILS")
echo "SUMMARY: banned matches=$found (S2 fatal=$s2, S3=$((found - s2)), allowed=$allowed, only-from ok=$checked)," \
     "crash hooks ours=$hooks_ours/2, failures=$nfail. $([ "$nfail" = 0 ] && echo PASS || echo FAIL)"
if [ "$nfail" != 0 ]; then echo; cat "$FAILS"; fi
} > "$OUT"

if [ -n "$REPORT" ]; then
    cp "$OUT" "$REPORT"
    grep '^SUMMARY' "$OUT"
    cat "$FAILS"
    [ -s "$FAILS" ] && echo "safety gate FAILED: no .nro is made. Full report: $REPORT"
else
    cat "$OUT"
fi
[ ! -s "$FAILS" ]
