#!/usr/bin/env bash
# The write-jail grep gate (docs/SAFETY.md S4, D14): only src/platform/SafeFs.cpp may call the functions that create, write,
# rename or delete files. Anywhere else in src/ they are refused, so every write has to go through SafeFs and its path check.
# tools/build.sh tests runs it (so CI's unit-test job does too). Exit 1 = a raw write call outside SafeFs.
#
# Usage:  tools/safety/check-writes.sh [<folder>]     (default: the repo's src/)
#         tools/safety/check-writes.sh --self-test    (proves the patterns catch what they should, and nothing more)
#
# What counts as a raw write (comments after // are ignored):
#   fopen/freopen/fdopen with a writing mode ("w", "a", "r+", ...), std::ofstream / std::fstream,
#   rename remove unlink rmdir mkdir truncate ftruncate creat symlink link chmod (plain, :: or std::),
#   std::filesystem / fs:: remove, rename, create_directory, copy, ..., open(..., O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND),
#   libnx's own file-system calls that write (fsFsCreateFile, fsFileWrite, fsFsDeleteFile, ...).
# Member functions with the same names (cache.remove(x), list->remove(y)) are fine: they aren't file calls.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ALLOWED_FILE="platform/SafeFs.cpp"   # relative to the scanned folder

PATTERNS=(
    '\b(fopen|freopen|fdopen)\s*\(.*"(w|a)[bxt+]*"'
    '\b(fopen|freopen|fdopen)\s*\(.*"r(\+[bt]?|[bt]\+)"'
    '(std::w?o?fstream\b|(^|[^A-Za-z0-9_:])w?o?fstream\s+[A-Za-z_])'
    '(^|[^A-Za-z0-9_.>:])(::|std::)?(rename|remove|unlink|unlinkat|rmdir|mkdir|mkdirat|truncate|ftruncate|creat|symlink|link|chmod)\s*\('
    '\b(filesystem|fs)::(remove|remove_all|rename|create_directory|create_directories|copy|copy_file|resize_file|permissions|create_symlink|create_hard_link|create_directory_symlink)\b'
    '(^|[^A-Za-z0-9_.>])(::)?open(at)?\s*\(.*O_(WRONLY|RDWR|CREAT|TRUNC|APPEND)'
    '\bfs(Fs|File|Dir)?[A-Za-z]*(CreateFile|CreateDirectory|DeleteFile|DeleteDirectory|RenameFile|RenameDirectory|Write|SetSize|Commit|CleanDirectory|Format)\s*\('
)

scan() {
    local dir="$1" hits=0
    while IFS= read -r -d '' file; do
        local rel="${file#"$dir"/}"
        [ "$rel" = "$ALLOWED_FILE" ] && continue
        # "<line number>:<tab><code>", // comments dropped (crude: a "//" inside a string cuts the line there, which only hides
        # text). The tab also stands in for "start of line" in the patterns.
        local code
        code=$(awk '{ sub(/\/\/.*/, ""); printf "%d:\t%s\n", NR, $0 }' "$file")
        for pattern in "${PATTERNS[@]}"; do
            local found
            found=$(printf '%s\n' "$code" | grep -E -- "^[0-9]+:.*$pattern" || true)
            if [ -n "$found" ]; then
                while IFS= read -r line; do
                    echo "  $rel:${line%%:*}: ${line#*:}"
                    hits=$((hits + 1))
                done <<< "$found"
            fi
        done
    done < <(find "$dir" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) -print0 | sort -z)
    return $((hits > 0))
}

self_test() {
    local tmp; tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/bad" "$tmp/good/platform"
    local bad=(
        'FILE* f = fopen(path, "w");'
        'std::FILE* f = std::fopen(p.c_str(), "ab");'
        'fopen(p, "r+");'
        'freopen("x", "wb", stdout);'
        'std::ofstream out("config.json");'
        'std::fstream both(p);'
        'rename(a, b);'
        'std::rename(a.c_str(), b.c_str());'
        '::unlink(p);'
        'if (remove(p) == 0) {}'
        'mkdir(dir, 0755);'
        'x = ::mkdir(d.c_str(), 0700);'
        'std::filesystem::remove_all(dir);'
        'fs::create_directories(dir);'
        'int fd = open(p, O_WRONLY | O_CREAT, 0644);'
        'int fd = ::open(p, O_RDWR);'
        'fsFsCreateFile(&fs, path, 0, 0);'
        'fsFileWrite(&file, 0, buf, n, FsWriteOption_None);'
        'fsFsDeleteFile(&fs, path);'
        'truncate(p, 0);'
    )
    local good=(
        'FILE* f = fopen(path, "rb");'
        'std::FILE* f = std::fopen(p.c_str(), "r");'
        'cache.remove(texture);'
        'list->remove(item);'
        'fs.renameFile(a, b);'
        'fs.removeFile(p);'
        'fs.makeDirs("logs");'
        'int fd = open(p, O_RDONLY);'
        '// rename(a, b) in a comment is fine'
        'auto x = removeTree(dir);'
        'std::string mkdirName = "x";'
    )
    local i=0 failures=0
    for line in "${bad[@]}"; do
        i=$((i + 1)); printf '%s\n' "$line" > "$tmp/bad/case$i.cpp"
        if scan "$tmp/bad" > /dev/null; then echo "self-test: NOT caught: $line"; failures=$((failures + 1)); fi
        rm "$tmp/bad/case$i.cpp"
    done
    for line in "${good[@]}"; do
        i=$((i + 1)); printf '%s\n' "$line" > "$tmp/good/case$i.cpp"
        if ! scan "$tmp/good" > /dev/null; then echo "self-test: wrongly caught: $line"; failures=$((failures + 1)); fi
        rm "$tmp/good/case$i.cpp"
    done
    printf '%s\n' 'FILE* f = fopen(path, "w");' > "$tmp/good/platform/SafeFs.cpp"  # the one file allowed to
    if ! scan "$tmp/good" > /dev/null; then echo "self-test: SafeFs.cpp itself was refused"; failures=$((failures + 1)); fi
    [ "$failures" = 0 ] && echo "check-writes self-test: ${#bad[@]} caught, ${#good[@]} let through, SafeFs.cpp allowed. PASS"
    return $((failures > 0))
}

if [ "${1:-}" = "--self-test" ]; then
    self_test
    exit $?
fi
DIR="${1:-$REPO/src}"
if scan "$DIR" > "$(mktemp)" 2>&1; then
    echo "check-writes: no raw file writes outside SafeFs in $DIR. PASS"
else
    echo "check-writes: raw file writes outside src/$ALLOWED_FILE (SAFETY.md S4: use platform::SafeFs instead):"
    scan "$DIR" || true
    exit 1
fi
