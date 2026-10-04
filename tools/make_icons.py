#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
"""OpenMail's Workbench icons: the program (an envelope) and its drawer.

    python3 tools/make_icons.py OUT_DIR      writes OpenMail.info and Drawer.info

Classic four-colour icons (OS 2.04 and later), the same format as
OpenPrint's package icons: one picture, complemented when selected."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

PENS = {".": 0, "#": 1, "w": 2, "b": 3}
NO_POSITION = -0x80000000
WBDRAWER, WBTOOL = 2, 3

ART = {
    "envelope": """
................................................
...##########################################...
...#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#..
...#w#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#w#..
...#ww##wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww##ww#..
...#wwww##wwwwwwwwwwwwwwwwwwwwwwwwwwwww##wwww#..
...#wwwwww##wwwwwwwwwwwwwwwwwwwwwwwww##wwwwww#..
...#wwwwwwww##wwwwwwwwwwwwwwwwwwwww##wwwwwwww#..
...#wwwwwwwwww##wwwwwwwwwwwwwwwww##wwwwwwwwww#..
...#wwwwwwwwwwww##wwwwwwwwwwwww##wwwwwwwwwwww#..
...#wwwwwwwwwwwwww###wwwwwww###wwwwwwwwwwwwww#..
...#wwwwwwwwwwwww##ww#######ww##wwwwwwwwwwwww#..
...#wwwwwwwwwww##wwwwwwwwwwwwwwww##wwwwwwwwww#..
...#wwwwwwwww##wwwwwwwwwwwwwwwwwwww##wwwwwwww#..
...#wwwwwww##wwwwwwwwwwwwwwwwwwwwwwww##wwwwww#..
...#wwwww##wwwwwwwwwwwwwwwwwwwwwwwwwwww##wwww#..
...#www##wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww##ww#..
...#w##wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#w#..
...#wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww#..
...##########################################...
....bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.
""",
    "drawer": """
..........................................
..####################################....
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..#bwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwb#..
..#bwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbwb#..
..#bwb##############################bwb#..
..#bwb#wwwwwwwwwwwwwwwwwwwwwwwwwwww#bwb#..
..#bwb#wwwwwwwwww########wwwwwwwwww#bwb#..
..#bwb#wwwwwwwwww#bbbbbb#wwwwwwwwww#bwb#..
..#bwb#wwwwwwwwww########wwwwwwwwww#bwb#..
..#bwb#wwwwwwwwwwwwwwwwwwwwwwwwwwww#bwb#..
..#bwb##############################bwb#..
..#bwbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbwb#..
..#bwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwb#..
..#bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb#..
..####################################....
""",
}


def picture(name):
    rows = ART[name].strip("\n").split("\n")
    w = max(len(r) for r in rows)
    return w, len(rows), [[PENS[c] for c in r.ljust(w, ".")] for r in rows]


def image_record(w, h, px):
    rowbytes = ((w + 15) // 16) * 2
    planes = bytearray()
    for plane in range(2):
        for y in range(h):
            row = bytearray(rowbytes)
            for x in range(w):
                if px[y][x] & (1 << plane):
                    row[x // 8] |= 0x80 >> (x % 8)
            planes += row
    return struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 0x03, 0, 0) + bytes(planes)


def icon(kind, art, stack=4096, window=(560, 120)):
    w, h, px = picture(art)
    drawer = kind == WBDRAWER
    out = bytearray(struct.pack(">HH", 0xE310, 1))
    out += struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h, 0x0004, 0x0003, 0x0001, 1, 0, 0, 0, 0, 0, 1)
    out += struct.pack(">BB", kind, 0)
    out += struct.pack(">IIiiIIi", 0, 0, NO_POSITION, NO_POSITION, 1 if drawer else 0, 0, stack)
    if drawer:
        nw = struct.pack(">hhhhBBIIIIIIIhhHHH", 60, 40, window[0], window[1], 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
        out += nw + struct.pack(">ii", 0, 0)
    out += image_record(w, h, px)
    if drawer:
        out += struct.pack(">IH", 0, 0)
    return bytes(out)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/icons")
    out.mkdir(parents=True, exist_ok=True)
    (out / "OpenMail.info").write_bytes(icon(WBTOOL, "envelope"))
    (out / "Drawer.info").write_bytes(icon(WBDRAWER, "drawer"))
    print(out / "OpenMail.info", out / "Drawer.info")
    return 0


if __name__ == "__main__":
    sys.exit(main())
