# KLayout-coredb

[KLayout](https://github.com/KLayout/klayout) workspace with [CORE](https://github.com/IHP-GmbH/CommonDB) (CommonDB) **mcore** streamer: open and save `.core` / `*.layout.core` layout files directly in KLayout — no GDS round-trip for viewing.

Vendored KLayout tree under `klayout-src/` plus `integrations/klayout/mcore` plugin sources (compiled into `db_plugins/libmcore.so` on Linux).

## Layout

| Path | Purpose |
|------|---------|
| `klayout-src/` | KLayout sources (`build.sh` / `build.bat`) |
| `integrations/klayout/mcore/` | CORE streamer plugin (reader/writer) |
| `integrations/klayout/generated/` | Pre-generated Cap'n Proto C++ |
| `../CommonDB` | CORE library sources linked at build time (`COMMONDB_ROOT`) |
| `scripts/setup_klayout_mcore.*` | Link `mcore` into KLayout tree + write `local.pri` |
| `scripts/test_core_load.rb` | Headless smoke test (`klayout -b -r …`) |

## Prerequisites

- **CommonDB** checkout as a sibling directory, e.g. `../CommonDB`
- **Qt 5** (`qmake`) on Linux, or Qt 6 on newer systems — KLayout auto-detects
- Build tools: `g++`, `make`, `git`, zlib/libpng/curl/expat devel headers

## Quick start (Linux)

```bash
git clone https://github.com/adatsuk/KLayout-coredb.git
git clone https://github.com/IHP-GmbH/CommonDB.git

cd KLayout-coredb
export COMMONDB_ROOT="$(cd ../CommonDB && pwd)"
bash scripts/setup_klayout_mcore.sh

cd klayout-src
./build.sh -build build-release -bin bin-release -noruby -nopython -option "-j$(nproc)"
```

Open a CORE layout:

```bash
./bin-release/klayout examples/sample.core   # or any *.layout.core from LibMan
```

Smoke test (after build, `klayout` on `PATH` or set `KLAYOUT`):

```bash
export COMMONDB_ROOT=../CommonDB
export KLAYOUT=$PWD/klayout-src/bin-release/klayout
$KLAYOUT -b -r scripts/test_core_load.rb
```

## Windows

```bat
scripts\setup_klayout_mcore.cmd
cd klayout-src
build.bat -j 4
bin-release\klayout.exe path\to\cell.layout.core
```

## Coordinate scale / naming

Layout views use `dbuPerMicron` in CORE. LibMan expects `cell.layout.core` naming — see [CommonDB CORE_FILE_NAMING](https://github.com/IHP-GmbH/CommonDB/blob/main/docs/CORE_FILE_NAMING.md).

## CI

GitHub Actions (`.github/workflows/ci.yaml`) builds on **Rocky Linux 8** and **Ubuntu 24.04**: checks out CommonDB, links `mcore`, runs `build.sh` (no Ruby/Python bindings), verifies `libmcore.so` and packages portable tarballs (`klayout-rocky8`, `klayout-linux-ubuntu24` artifacts).

## Upstream

KLayout sources track [KLayout/klayout](https://github.com/KLayout/klayout). CORE plugin sources mirror [CommonDB integrations/klayout](https://github.com/IHP-GmbH/CommonDB/tree/main/integrations/klayout).

## License

KLayout is GPL-2.0 (see `klayout-src/COPYRIGHT`). CORE integration follows the CommonDB / IHP stack workflow.
