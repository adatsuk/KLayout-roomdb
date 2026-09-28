#!/usr/bin/env bash
# Link the ROOM mroom streamer into the vendored KLayout tree and write local.pri.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KLAYOUT="${1:-$ROOT/klayout-src}"
if [[ "$KLAYOUT" != /* ]]; then
  KLAYOUT="$(cd "$(dirname "$KLAYOUT")" && pwd)/$(basename "$KLAYOUT")"
fi

if [[ -n "${COMMONDB_ROOT:-}" ]]; then
  COMMONDB="$(cd "$COMMONDB_ROOT" && pwd)"
elif [[ -d "$ROOT/../CommonDB" ]]; then
  COMMONDB="$(cd "$ROOT/../CommonDB" && pwd)"
else
  echo "COMMONDB_ROOT not set and ../CommonDB not found" >&2
  exit 1
fi

STREAMERS="$KLAYOUT/src/plugins/streamers"
MROOM="$ROOT/integrations/klayout/mroom"
LINK="$STREAMERS/mroom"

if [[ ! -d "$KLAYOUT/src" ]]; then
  echo "KLayout not found at $KLAYOUT" >&2
  exit 1
fi

mkdir -p "$STREAMERS"
if [[ -L "$LINK" ]] || [[ -e "$LINK" ]]; then
  rm -rf "$LINK"
fi
ln -s "$MROOM" "$LINK"

mkdir -p "$MROOM/db_plugin"
cat > "$MROOM/db_plugin/local.pri" <<EOF
COMMONDB_ROOT = $COMMONDB
KLAYOUT_SRC = $KLAYOUT/src
EOF

# Keep mroom out of auto-discovery so SUBDIRS lists it once, after lstream.
STREAMERS_PRO="$STREAMERS/streamers.pro"
if ! grep -q 'SUBDIR_LIST -= \$\$PWD/mroom' "$STREAMERS_PRO" 2>/dev/null; then
  sed -i '/SUBDIR_LIST -= \$\$PWD\/streamers.pro/a SUBDIR_LIST -= $$PWD/mroom' "$STREAMERS_PRO"
fi
if ! grep -q 'SUBDIRS += mroom' "$STREAMERS_PRO" 2>/dev/null; then
  printf '\n# CommonDB ROOM streamer\nSUBDIRS += mroom\n' >> "$STREAMERS_PRO"
fi

echo "Linked $LINK -> $MROOM"
echo "COMMONDB_ROOT=$COMMONDB"
echo "KLAYOUT_SRC=$KLAYOUT/src"

bash "$(dirname "$0")/regenerate_klayout_capnp.sh"
