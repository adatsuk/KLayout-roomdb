#!/usr/bin/env bash
# Regenerate Cap'n Proto C++ for the KLayout mroom plugin from CommonDB schema.
set -euo pipefail

if [[ -n "${COMMONDB_ROOT:-}" ]]; then
  COMMONDB="$(cd "$COMMONDB_ROOT" && pwd)"
elif [[ -d "$(dirname "$0")/../CommonDB" ]]; then
  COMMONDB="$(cd "$(dirname "$0")/../CommonDB" && pwd)"
elif [[ -d "$(dirname "$0")/../../CommonDB" ]]; then
  COMMONDB="$(cd "$(dirname "$0")/../../CommonDB" && pwd)"
else
  echo "COMMONDB_ROOT not set and CommonDB not found" >&2
  exit 1
fi

CAPNP_ROOT="${CAPNP_ROOT:-$COMMONDB/third_party/capnp-install}"
if [[ ! -x "$CAPNP_ROOT/bin/capnp" ]]; then
  CAPNP_ROOT="$COMMONDB/third_party/capnp-install-linux"
fi

CAPNP_BIN="$CAPNP_ROOT/bin/capnp"
CAPNPC_CXX="$CAPNP_ROOT/bin/capnpc-c++"
SCHEMA_DIR="$COMMONDB/schema"
GEN_DIR="$COMMONDB/integrations/klayout/generated"

if [[ ! -x "$CAPNP_BIN" || ! -x "$CAPNPC_CXX" ]]; then
  echo "Cap'n Proto tools not found under $CAPNP_ROOT" >&2
  exit 1
fi

mkdir -p "$GEN_DIR"
cd "$SCHEMA_DIR"

"$CAPNP_BIN" compile \
  -o"$CAPNPC_CXX:$GEN_DIR" \
  -I"$CAPNP_ROOT/include" \
  -I"$SCHEMA_DIR" \
  common.capnp dm.capnp design.capnp database.capnp views.capnp index.capnp compact.capnp

echo "Regenerated Cap'n Proto sources in $GEN_DIR"
