#!/usr/bin/env bash
# Build KLayout with the ROOM mroom streamer (Linux / WSL).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KLAYOUT="${KLAYOUT_ROOT:-$SCRIPT_DIR/klayout-src}"

bash "$SCRIPT_DIR/scripts/setup_klayout_mroom.sh" "$KLAYOUT"

cd "$KLAYOUT"
sed -i 's/\r$//' build.sh version.sh 2>/dev/null || true
chmod +x build.sh version.sh 2>/dev/null || true

bash build.sh \
  -build build-release \
  -bin bin-release \
  -noruby \
  -nopython \
  -nolibgit2 \
  -option "-j$(nproc)"

test -x bin-release/klayout
echo "==> Built: $KLAYOUT/bin-release/klayout"
