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

# Windows/MinGW qmake does not follow MSYS symlinks for exists()/$$PWD, so
# local.pri and room.pri would look missing. Use a real copy on MSYS/MINGW.
_is_windows=0
case "$(uname -s 2>/dev/null || true)" in
  MINGW*|MSYS*|CYGWIN*) _is_windows=1 ;;
esac
if [[ -n "${MSYSTEM:-}" ]]; then
  _is_windows=1
fi

if [[ "$_is_windows" -eq 1 ]]; then
  cp -a "$MROOM" "$LINK"
  _pri_dir="$LINK/db_plugin"
  if command -v cygpath >/dev/null 2>&1; then
    COMMONDB="$(cygpath -m "$COMMONDB")"
    KLAYOUT="$(cygpath -m "$KLAYOUT")"
  fi
else
  ln -s "$MROOM" "$LINK"
  _pri_dir="$MROOM/db_plugin"
fi

mkdir -p "$_pri_dir" "$MROOM/db_plugin"
_pri_body=$(cat <<EOF
COMMONDB_ROOT = $COMMONDB
KLAYOUT_SRC = $KLAYOUT/src
EOF
)
printf '%s\n' "$_pri_body" > "$_pri_dir/local.pri"
# Keep the source tree copy in sync for non-Windows / editors.
printf '%s\n' "$_pri_body" > "$MROOM/db_plugin/local.pri"

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
