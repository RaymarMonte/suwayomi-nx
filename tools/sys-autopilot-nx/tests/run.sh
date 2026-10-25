#!/bin/sh
# Builds and runs the host-side test suite (no devkitPro required).
set -e
cd "$(dirname "$0")"

SRC=../source/common
JSMN=../lib/jsmn
OUT=build
FAKE_SD=/tmp/sys-autopilot-fakesd

mkdir -p "$OUT"

CFLAGS="-g -Wall -Wextra -Wno-unused-parameter -I$SRC -I$JSMN"

echo "== test_http =="
cc $CFLAGS -o "$OUT/test_http" test_http.c "$SRC/http.c" "$SRC/base64.c"
"$OUT/test_http"

echo "== test_core =="
cc $CFLAGS -o "$OUT/test_core" test_core.c \
    "$SRC/base64.c" "$SRC/buttons.c" "$SRC/json.c" "$SRC/jstream.c" "$SRC/sha256.c"
"$OUT/test_core"

echo "== test_touch (with AddressSanitizer + UBSan) =="
cc $CFLAGS -fsanitize=address,undefined -fno-omit-frame-pointer \
    -o "$OUT/test_touch" test_touch.c "$SRC/apiargs.c" "$SRC/json.c" "$SRC/buttons.c"
"$OUT/test_touch"

echo "== test_mdns =="
cc $CFLAGS -o "$OUT/test_mdns" test_mdns.c \
    "$SRC/mdns.c" "$SRC/device_info.c" "$SRC/config.c" "$SRC/netif.c"
"$OUT/test_mdns"

echo "== test_mcp =="
cc $CFLAGS -Ifake \
    -DFILES_ROOT="\"$FAKE_SD\"" \
    -DMCP_UPLOAD_TMP="\"$FAKE_SD/.upload.tmp\"" \
    -DFAKE_SD="\"$FAKE_SD\"" \
    -o "$OUT/test_mcp" test_mcp.c stubs.c \
    "$SRC/mcp.c" "$SRC/http.c" "$SRC/base64.c" "$SRC/json.c" "$SRC/jstream.c" \
    "$SRC/buttons.c" "$SRC/apiargs.c" "$SRC/files.c" "$SRC/sha256.c" \
    "$SRC/config.c" "$SRC/battery.c"
"$OUT/test_mcp"

echo "ALL HOST TESTS PASSED"
