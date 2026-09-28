#!/usr/bin/env bash
# Bootstrap Cap'n Proto, link mroom, and build KLayout (shared CI entry point).
set -euo pipefail

WS="${1:?GitHub workspace root containing KLayout-roomdb and CommonDB}"
JOBS="${2:-$(nproc)}"

export COMMONDB_ROOT="$WS/CommonDB"
export CAPNP_ROOT="$COMMONDB_ROOT/third_party/capnp-install-linux"

ROOT="$WS/KLayout-roomdb"
KLAYOUT="$ROOT/klayout-src"

sed -i 's/\r$//' "$COMMONDB_ROOT/scripts/build_capnp_linux.sh" || true
CAPNP_SKIP_CHECK=1 bash "$COMMONDB_ROOT/scripts/build_capnp_linux.sh" \
  "https://github.com/capnproto/capnproto.git" branch master "" "" \
  "$COMMONDB_ROOT/third_party/capnproto-linux" \
  "$COMMONDB_ROOT/third_party/capnp-install-linux"
ln -sfn capnp-install-linux "$COMMONDB_ROOT/third_party/capnp-install"

sed -i 's/\r$//' "$ROOT/scripts/regenerate_klayout_capnp.sh" || true
bash "$ROOT/scripts/regenerate_klayout_capnp.sh"

sed -i 's/\r$//' "$ROOT/scripts/"*.sh "$KLAYOUT/build.sh" || true
chmod +x "$KLAYOUT/build.sh" "$KLAYOUT/version.sh" "$ROOT/scripts/"*.sh
bash "$ROOT/scripts/setup_klayout_mroom.sh" "$KLAYOUT"

(
  cd "$KLAYOUT"
  bash build.sh -build build-release -bin bin-release -noruby -nopython -nolibgit2 -option "-j${JOBS}"
)

bash "$ROOT/scripts/verify_klayout_mroom.sh" "$KLAYOUT"
