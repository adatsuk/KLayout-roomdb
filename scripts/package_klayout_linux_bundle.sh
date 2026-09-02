#!/usr/bin/env bash
# Package KLayout + mcore + Qt5/Cap'n Proto runtimes into a portable tar.gz (RHEL 8 / Rocky 8).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="${1:-$ROOT/klayout-src/bin-release}"
OUT_TAR="${2:-$ROOT/klayout-rocky8-bundle.tar.gz}"
BUNDLE_LABEL="${3:-Rocky Linux 8}"
CAPNP_ROOT="${CAPNP_ROOT:-}"

KLAYOUT_BIN="$BIN_DIR/klayout"
if [[ ! -x "$KLAYOUT_BIN" ]]; then
    echo "ERROR: klayout not found at $KLAYOUT_BIN (run build.sh first)"
    exit 1
fi

DIST="$ROOT/dist-bundle"
STAGE="$ROOT/.bundle-stage"
rm -rf "$DIST" "$STAGE"
mkdir -p "$STAGE" "$DIST"

cp -a "$BIN_DIR"/. "$STAGE/"

if [[ -n "$CAPNP_ROOT" && -d "$CAPNP_ROOT/lib" ]]; then
    mkdir -p "$STAGE/lib"
    cp -a "$CAPNP_ROOT/lib"/libkj*.so* "$STAGE/lib/" 2>/dev/null || true
    cp -a "$CAPNP_ROOT/lib"/libcapnp*.so* "$STAGE/lib/" 2>/dev/null || true
fi

QMAKE="$(command -v qmake-qt5 2>/dev/null || command -v qmake)"
export LD_LIBRARY_PATH="${STAGE}/lib:${LD_LIBRARY_PATH:-}"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN:$ORIGIN/lib:$ORIGIN/db_plugins' "$STAGE/klayout" || true
    if [[ -f "$STAGE/db_plugins/libmcore.so" ]]; then
        patchelf --set-rpath '$ORIGIN:$ORIGIN/..:$ORIGIN/../lib' "$STAGE/db_plugins/libmcore.so" || true
    fi
fi

if command -v linuxdeployqt >/dev/null 2>&1; then
    linuxdeployqt "$STAGE/klayout" -qmake="$QMAKE" -bundle-non-qt-libs -always-overwrite
else
    echo "WARNING: linuxdeployqt not found; copying dependencies via ldd."
    mkdir -p "$STAGE/lib"
    while IFS= read -r lib; do
        [[ -n "$lib" && -f "$lib" ]] || continue
        cp -Ln "$lib" "$STAGE/lib/" 2>/dev/null || cp -L "$lib" "$STAGE/lib/" || true
    done < <(ldd "$STAGE/klayout" | awk '/=>/ {print $3}' | grep -vE '^/(lib|usr/lib)' || true)
    QT_PLUGINS="$(qmake -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
    if [[ -n "$QT_PLUGINS" && -d "$QT_PLUGINS" ]]; then
        cp -a "$QT_PLUGINS" "$STAGE/"
    fi
fi

cp -a "$STAGE"/. "$DIST/"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN:$ORIGIN/lib:$ORIGIN/db_plugins' "$DIST/klayout" || true
fi

cat >"$DIST/klayout-run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
cd "$DIR"
exec "$DIR/klayout" "$@"
EOF
chmod +x "$DIST/klayout-run.sh"

cat >"$DIST/BUNDLE.txt" <<EOF
KLayout + CORE (mcore) portable bundle ($BUNDLE_LABEL)
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Run: ./klayout-run.sh [file.layout.core]

Requires: Linux x86_64 with glibc 2.28+ (RHEL 8 / Rocky Linux 8 / AlmaLinux 8).
Includes mcore streamer for .core / *.layout.core files.
EOF

rm -rf "$STAGE"
tar -C "$ROOT" -czf "$OUT_TAR" "$(basename "$DIST")"
echo "Created $OUT_TAR"
