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
  # Door drives KLayout with Python macros (pya / -rr *.py). Keep -noruby to
  # shrink the build, but Python bindings are required.
  # Pick a python that has both the interpreter and headers (Python.h).
  # On Rocky 8, a bare `python3.12` binary may exist without -devel.
  pick_python() {
    local c inc
    for c in python3.12 python3.11 python3.9 python3.8 python3; do
      command -v "$c" >/dev/null 2>&1 || continue
      inc="$("$c" -c 'import sysconfig; print(sysconfig.get_path("include"))' 2>/dev/null || true)"
      if [[ -n "$inc" && -f "$inc/Python.h" ]]; then
        echo "$c"
        return 0
      fi
    done
    echo python3
  }
  py="$(pick_python)"
  echo "Building KLayout with Python: $py ($("$py" -V 2>&1))"
  bash build.sh -build build-release -bin bin-release -noruby -nolibgit2 \
    -python "$py" -option "-j${JOBS}"
)

bash "$ROOT/scripts/verify_klayout_mroom.sh" "$KLAYOUT"
