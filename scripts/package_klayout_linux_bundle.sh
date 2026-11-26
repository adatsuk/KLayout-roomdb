#!/usr/bin/env bash
# Package KLayout + mroom + Qt5/Cap'n Proto runtimes into a portable tar.gz (RHEL 8 / Rocky 8).
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

mkdir -p "$STAGE/lib"
bundle_capnp_libs() {
    local src="$1"
    [[ -n "$src" && -d "$src" ]] || return 1
    local n=0
    local f
    for f in "$src"/libkj*.so* "$src"/libcapnp*.so*; do
        [[ -e "$f" ]] || continue
        cp -a "$f" "$STAGE/lib/" || true
        n=$((n + 1))
    done
    [[ "$n" -gt 0 ]]
}

# mroom links Cap'n Proto (SONAME libcapnp-*-dev.so / libkj-*-dev.so). Must ship in lib/.
if [[ -n "$CAPNP_ROOT" ]]; then
    if bundle_capnp_libs "$CAPNP_ROOT/lib"; then
        echo "Bundled Cap'n Proto from CAPNP_ROOT=$CAPNP_ROOT"
    else
        echo "WARNING: CAPNP_ROOT=$CAPNP_ROOT has no libkj*/libcapnp* — will try ldd paths" >&2
    fi
fi

QMAKE="$(command -v qmake-qt5 2>/dev/null || command -v qmake)"
DEPLOY_LDPATH="${STAGE}:${STAGE}/db_plugins:${STAGE}/lay_plugins"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
    DEPLOY_LDPATH="${DEPLOY_LDPATH}:${LD_LIBRARY_PATH}"
fi
if [[ -n "$CAPNP_ROOT" && -d "$CAPNP_ROOT/lib" ]]; then
    DEPLOY_LDPATH="${CAPNP_ROOT}/lib:${DEPLOY_LDPATH}"
fi
DEPLOY_LDPATH="${DEPLOY_LDPATH}:/usr/lib64:/lib64:/usr/lib/x86_64-linux-gnu"
export LD_LIBRARY_PATH="${STAGE}/lib:${DEPLOY_LDPATH}"

patch_plugin_rpath() {
    local so="$1"
    [[ -f "$so" ]] || return 0
    command -v patchelf >/dev/null 2>&1 || return 0
    patchelf --set-rpath '$ORIGIN:$ORIGIN/..:$ORIGIN/../lib' "$so" || true
}

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN:$ORIGIN/lib:$ORIGIN/db_plugins:$ORIGIN/lay_plugins' "$STAGE/klayout" || true
    patch_plugin_rpath "$STAGE/db_plugins/libmroom.so"
    patch_plugin_rpath "$STAGE/lay_plugins/libmroom_ui.so"
fi

bundle_ldd() {
    echo "Bundling Qt/runtime/Python dependencies via ldd."
    mkdir -p "$STAGE/lib"
    copy_deps() {
        local bin="$1" include_system_python="${2:-0}"
        local lib
        while IFS= read -r lib; do
            [[ -n "$lib" && -f "$lib" ]] || continue
            # System libs stay on the host — except python/capnp/kj/crypto that mroom/pya need.
            if [[ "$include_system_python" != "1" ]]; then
                case "$lib" in
                    /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*)
                        case "$(basename "$lib")" in
                            libpython*|libcapnp*|libkj*|libcrypto.so*) ;;
                            *) continue ;;
                        esac
                        ;;
                esac
            fi
            cp -Ln "$lib" "$STAGE/lib/" 2>/dev/null || cp -L "$lib" "$STAGE/lib/" || true
        done < <(ldd "$bin" 2>/dev/null | awk '/=>/ {print $3}' || true)
    }
    copy_deps "$STAGE/klayout"
    copy_deps "$STAGE/db_plugins/libmroom.so"
    copy_deps "$STAGE/lay_plugins/libmroom_ui.so"
    # Explicitly ship every libpython KLayout links (pya interpreter).
    local py
    while IFS= read -r py; do
        [[ -n "$py" && -f "$py" ]] || continue
        cp -Ln "$py" "$STAGE/lib/" 2>/dev/null || cp -L "$py" "$STAGE/lib/" || true
        echo "Bundled Python: $(basename "$py")"
    done < <(ldd "$STAGE/klayout" 2>/dev/null | awk '/libpython/ {print $3}' || true)
    # Cap'n Proto: also chase unresolved sonames via ldconfig / CAPNP_ROOT.
    local miss
    while IFS= read -r miss; do
        [[ -n "$miss" ]] || continue
        local hit=""
        if [[ -n "$CAPNP_ROOT" && -e "$CAPNP_ROOT/lib/$miss" ]]; then
            hit="$CAPNP_ROOT/lib/$miss"
        else
            hit="$(ldconfig -p 2>/dev/null | awk -v n="$miss" '$1 == n {print $NF; exit}')"
        fi
        if [[ -n "$hit" && -f "$hit" ]]; then
            cp -Ln "$hit" "$STAGE/lib/" 2>/dev/null || cp -L "$hit" "$STAGE/lib/" || true
            echo "Bundled Cap'n Proto dep: $(basename "$hit")"
        fi
    done < <(ldd "$STAGE/db_plugins/libmroom.so" 2>/dev/null | awk '/not found/ {print $1}' || true)
    QT_PLUGINS="$(qmake -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
    if [[ -n "$QT_PLUGINS" && -d "$QT_PLUGINS" ]]; then
        cp -a "$QT_PLUGINS" "$STAGE/"
    fi
}

if [[ "${BUNDLE_SKIP_LINUXDEPLOYQT:-}" == "1" ]]; then
    bundle_ldd
elif command -v linuxdeployqt >/dev/null 2>&1; then
    if ! linuxdeployqt "$STAGE/klayout" -qmake="$QMAKE" -bundle-non-qt-libs -always-overwrite; then
        echo "linuxdeployqt failed; falling back to ldd bundling." >&2
        bundle_ldd
    fi
else
    echo "WARNING: linuxdeployqt not found; copying dependencies via ldd."
    bundle_ldd
fi

cp -a "$STAGE"/. "$DIST/"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN:$ORIGIN/lib:$ORIGIN/db_plugins:$ORIGIN/lay_plugins' "$DIST/klayout" || true
    patch_plugin_rpath "$DIST/db_plugins/libmroom.so"
    patch_plugin_rpath "$DIST/lay_plugins/libmroom_ui.so"
fi

# Fail the package step if mroom still cannot resolve Cap'n Proto — Door hosts
# will otherwise show "Unable to load plugin: libmroom.so".
if [[ -f "$DIST/db_plugins/libmroom.so" ]]; then
    export LD_LIBRARY_PATH="$DIST:$DIST/lib:$DIST/db_plugins:$DIST/lay_plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    missing="$(ldd "$DIST/db_plugins/libmroom.so" 2>/dev/null | awk '/not found/ {print $1}' || true)"
    if [[ -n "$missing" ]]; then
        echo "ERROR: libmroom.so has unresolved deps after packaging:" >&2
        echo "$missing" >&2
        echo "CAPNP_ROOT=${CAPNP_ROOT:-<unset>}" >&2
        ls -la "${CAPNP_ROOT:-/nonexistent}/lib" 2>&1 | head -40 >&2 || true
        exit 1
    fi
    echo "OK: libmroom.so dependencies resolved in bundle"
fi

cat >"$DIST/klayout-run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
# Resolve symlinks so DIR is the real bundle even when invoked via <prefix>/bin/klayout.
_src="${BASH_SOURCE[0]}"
while [[ -L "$_src" ]]; do
  _dir="$(cd -P "$(dirname "$_src")" && pwd)"
  _src="$(readlink "$_src")"
  [[ "$_src" != /* ]] && _src="$_dir/$_src"
done
DIR="$(cd -P "$(dirname "$_src")" && pwd)"
# $DIR first: libklayout_*.so and plugins live next to the binary, not only in lib/.
export LD_LIBRARY_PATH="$DIR:$DIR/lib:$DIR/db_plugins:$DIR/lay_plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
# pymod/pya is required for `import pya` (Door -rr macros).
if [[ -d "$DIR/pymod" ]]; then
  export PYTHONPATH="$DIR/pymod${PYTHONPATH:+:$PYTHONPATH}"
fi
cd "$DIR"
exec "$DIR/klayout" "$@"
EOF
chmod +x "$DIST/klayout-run.sh"

cat >"$DIST/BUNDLE.txt" <<EOF
KLayout + ROOM (mroom) portable bundle ($BUNDLE_LABEL)
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Run: ./klayout-run.sh [file.layout.room]

Requires: Linux x86_64 with glibc 2.28+ (RHEL 8 / Rocky Linux 8 / AlmaLinux 8).
Includes mroom streamer for .room / *.layout.room files.
Includes Python (pya) for Door macros (-rr); pymod/ is on PYTHONPATH.
EOF

rm -rf "$STAGE"
tar -C "$ROOT" -czf "$OUT_TAR" "$(basename "$DIST")"
echo "Created $OUT_TAR"
