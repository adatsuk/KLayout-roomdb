# KLayout integration — read/write `.room` directly

ROOM is compiled **into** a KLayout `db_plugins/mroom.dll` streamer (no separate `libroom` link). Cap'n Proto comes from KLayout's LStream runtime (`xcapnp` / `xkj`) or from `third_party/capnp-install` when built standalone.

## Why `mroom`?

`mroom` is the **internal KLayout streamer id** (folder name, `TARGET`, DLL name: `mroom.dll` / `mroom_ui.dll`). The user-visible format is **ROOM** (`*.layout.room`, title “CommonDB ROOM” in the file dialog).

The name sorts **after** `lstream` in KLayout's alphabetical `SUBDIRS` order, so `xcapnp` / `xkj` are already linked when `mroom.dll` is built.

## Setup

From this repository:

```bat
scripts\setup_klayout_mroom.cmd
```

```bash
bash scripts/setup_klayout_mroom.sh
```

That links `integrations/klayout/mroom` into `klayout-src/src/plugins/streamers/mroom`, writes `local.pri`, and keeps a single `SUBDIRS += mroom` entry in `streamers.pro`.

Rebuild:

```bat
cd klayout-src
build.bat -j 4
```

## Layout XOR with ROOM (LibMan)

LibMan’s **XOR… / XOR with …** batch script (`klayout -b -r`) calls `Layout.read()` on both operands. With **mroom** installed in that same `klayout.exe`, operands may be `*.layout.room` as well as GDS/OAS/LStream.

If XOR fails on `.room` with a read error, confirm Tool Manager → Layout points at the KLayout build that includes `mroom.dll`.

## Files

| Path | Role |
|------|------|
| `integrations/klayout/mroom/room.pri` | Lists CommonDB `src/*.cpp` + generated capnp |
| `integrations/klayout/mroom/db_plugin/roomReader.cc` | `Database::loadFromFile` → `db::Layout` |
| `integrations/klayout/mroom/db_plugin/roomWriter.cc` | `db::Layout` → `Database::saveToFile` (compact) |
| `integrations/klayout/generated/` | Pre-generated capnp C++ (committed) |
| `integrations/klayout/streamers.pro.patch` | KLayout `streamers.pro` wiring |
