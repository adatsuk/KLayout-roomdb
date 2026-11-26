# -*- coding: utf-8 -*-
"""Smoke-load a .room file via KLayout Python (pya) + mroom streamer."""
import os
import sys

import pya

repo = os.environ.get("COMMONDB_ROOT") or os.environ.get("ROOM_ROOT") or ""
if not repo:
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "CommonDB"))

path = os.environ.get("ROOM_PATH") or os.path.join(
    repo, "examples", "gds_to_room", "output", "sample.room"
)
if not os.path.isfile(path):
    print(f"SKIP: sample room not found: {path}")
    sys.exit(0)

ly = pya.Layout()
ly.read(path)
print(f"file={path}")
print(f"cells={ly.cells()}")
top = ly.top_cell()
print(f"top={top.name if top is not None else ''}")
print(f"layers={ly.layers()}")
print("OK")
