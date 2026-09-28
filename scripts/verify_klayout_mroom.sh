#!/usr/bin/env bash
# Verify the KLayout ROOM (mroom) db plugin was built and installed.
set -euo pipefail

KLAYOUT_SRC="${1:?KLayout source tree (klayout-src)}"

check_dir() {
    local dir="$1"
    [[ -f "$dir/libmroom.so" || -f "$dir/libmroom.so.0" || -L "$dir/libmroom.so" ]]
}

if check_dir "$KLAYOUT_SRC/bin-release/db_plugins"; then
    echo "OK: libmroom in $KLAYOUT_SRC/bin-release/db_plugins"
    exit 0
fi

if check_dir "$KLAYOUT_SRC/build-release/db_plugins"; then
    echo "OK: libmroom in $KLAYOUT_SRC/build-release/db_plugins"
    exit 0
fi

if find "$KLAYOUT_SRC" \( -name 'libmroom.so' -o -name 'libmroom.so.*' -o -name 'mroom.dll' \) -print -quit | grep -q .; then
    echo "OK: mroom plugin found under $KLAYOUT_SRC"
    exit 0
fi

echo "ERROR: mroom db plugin not found (expected libmroom.so under db_plugins)" >&2
find "$KLAYOUT_SRC" -path '*/db_plugins/*' -maxdepth 4 -type f 2>/dev/null | head -20 >&2 || true
exit 1
