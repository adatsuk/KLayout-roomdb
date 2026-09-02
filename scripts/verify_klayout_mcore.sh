#!/usr/bin/env bash
# Verify KLayout CORE (mcore) db plugin was built and installed.
set -euo pipefail

KLAYOUT_SRC="${1:?KLayout source tree (klayout-src)}"

check_dir() {
    local dir="$1"
    [[ -f "$dir/libmcore.so" || -f "$dir/libmcore.so.0" || -L "$dir/libmcore.so" ]]
}

if check_dir "$KLAYOUT_SRC/bin-release/db_plugins"; then
    echo "OK: libmcore in $KLAYOUT_SRC/bin-release/db_plugins"
    exit 0
fi

if check_dir "$KLAYOUT_SRC/build-release/db_plugins"; then
    echo "OK: libmcore in $KLAYOUT_SRC/build-release/db_plugins"
    exit 0
fi

if find "$KLAYOUT_SRC" \( -name 'libmcore.so' -o -name 'libmcore.so.*' -o -name 'mcore.dll' \) -print -quit | grep -q .; then
    echo "OK: mcore plugin found under $KLAYOUT_SRC"
    exit 0
fi

echo "ERROR: mcore db plugin not found (expected libmcore.so under db_plugins)" >&2
find "$KLAYOUT_SRC" -path '*/db_plugins/*' -maxdepth 4 -type f 2>/dev/null | head -20 >&2 || true
exit 1
