# KLayout-roomdb

[KLayout](https://github.com/KLayout/klayout) workspace with the [ROOM](https://github.com/IHP-GmbH/CommonDB) (CommonDB) **mroom** streamer: open and save `.room` / `*.layout.room` layout files directly in KLayout — no GDS round-trip for viewing.

Vendored KLayout tree under `klayout-src/` (0.30.12) plus `integrations/klayout/mroom` plugin sources (compiled into `db_plugins/libmroom.so` on Linux, `mroom.dll` on Windows).

## Layout

| Path | Purpose |
|------|---------|
| `klayout-src/` | KLayout sources (`build.sh` / `build.bat`) |
| `integrations/klayout/mroom/` | ROOM streamer plugin (reader/writer) |
| `integrations/klayout/generated/` | Pre-generated Cap'n Proto C++ |
| `../CommonDB` | ROOM library sources linked at build time (`COMMONDB_ROOT`) |
| `scripts/setup_klayout_mroom.*` | Link `mroom` into the KLayout tree and write `local.pri` |
| `scripts/test_room_load.rb` | Headless smoke test (`klayout -b -r …`) |

## Prerequisites

- **CommonDB** checkout as a sibling directory, e.g. `../CommonDB`
- **Qt 5** (`qmake`) on Linux, or Qt 6 on newer systems — KLayout auto-detects
- Build tools: `g++`, `make`, `git`, zlib/libpng/curl/expat devel headers

## Quick start (Linux)

```bash
git clone https://github.com/adatsuk/KLayout-roomdb.git
git clone https://github.com/IHP-GmbH/CommonDB.git

cd KLayout-roomdb
export COMMONDB_ROOT="$(cd ../CommonDB && pwd)"
bash scripts/setup_klayout_mroom.sh

cd klayout-src
./build.sh -build build-release -bin bin-release -noruby -nopython -option "-j$(nproc)"
```

Open a ROOM layout:

```bash
./bin-release/klayout path/to/cell.layout.room
```

Smoke test (after build):

```bash
export COMMONDB_ROOT=../CommonDB
export KLAYOUT=$PWD/klayout-src/bin-release/klayout
$KLAYOUT -b -r scripts/test_room_load.rb
```

## Windows

```bat
scripts\setup_klayout_mroom.cmd
cd klayout-src
build.bat -j 4
bin-release\klayout.exe path\to\cell.layout.room
```

## Coordinate scale / naming

Layout views use `dbuPerMicron` in ROOM. LibMan expects `cell.layout.room` naming — see [CommonDB ROOM_FILE_NAMING](https://github.com/IHP-GmbH/CommonDB/blob/main/docs/ROOM_FILE_NAMING.md).

## CI

GitHub Actions (`.github/workflows/ci.yaml`) builds on **Rocky Linux 8** and **Ubuntu 24.04**: checks out CommonDB, links `mroom`, runs `build.sh` (no Ruby/Python bindings), verifies `libmroom.so` and packages portable tarballs (`klayout-rocky8`, `klayout-linux-ubuntu24` artifacts).

## Upstream

KLayout sources track [KLayout/klayout](https://github.com/KLayout/klayout) release **0.30.12**. The ROOM plugin mirrors [CommonDB integrations/klayout](https://github.com/IHP-GmbH/CommonDB/tree/main/integrations/klayout).

## License

KLayout is GPL-2.0 (see `klayout-src/COPYRIGHT`). ROOM integration follows the CommonDB / IHP stack workflow.
