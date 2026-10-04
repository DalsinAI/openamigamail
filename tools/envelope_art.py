#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
"""OpenMail's icon picture in full colour (RGBA), drawn at 4x and reduced,
for tools/make_icons.js to turn into an OS 3.2-style colour icon.
    python3 tools/envelope_art.py OUT.rgba    (48 x 40, raw RGBA)"""
import sys
from PIL import Image, ImageDraw

W, H, S = 48, 40, 4
img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
x0, y0, x1, y1 = 3 * S, 8 * S, 44 * S, 35 * S
# shadow, body, outline
d.rounded_rectangle((x0 + 2 * S, y0 + 2 * S, x1 + 2 * S, y1 + 2 * S), radius=2 * S, fill=(40, 40, 60, 110))
d.rounded_rectangle((x0, y0, x1, y1), radius=2 * S, fill=(250, 246, 232, 255), outline=(48, 48, 64, 255), width=S)
# the lower folds
d.line((x0 + S, y1 - S, (x0 + x1) // 2, y0 + 15 * S), fill=(176, 170, 156, 255), width=S)
d.line((x1 - S, y1 - S, (x0 + x1) // 2, y0 + 15 * S), fill=(176, 170, 156, 255), width=S)
# the flap, shaded
d.polygon([(x0 + S, y0 + S), (x1 - S, y0 + S), ((x0 + x1) // 2, y0 + 15 * S)], fill=(228, 220, 198, 255), outline=(96, 92, 110, 255))
# a stamp: red with a white edge
sx, sy = x1 - 10 * S, y0 + 3 * S
d.rectangle((sx, sy, sx + 7 * S, sy + 8 * S), fill=(255, 255, 255, 255), outline=(150, 40, 40, 255), width=S // 2)
d.rectangle((sx + S, sy + S, sx + 6 * S, sy + 7 * S), fill=(210, 50, 50, 255))
# an "@" seal in Amiga blue, bottom left
cx, cy, r = x0 + 8 * S, y1 - 7 * S, 4 * S
d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(40, 90, 200, 255), outline=(20, 40, 110, 255), width=S // 2)
d.ellipse((cx - r // 2, cy - r // 2, cx + r // 2, cy + r // 2), outline=(255, 255, 255, 255), width=S)
img = img.resize((W, H), Image.LANCZOS)
px = img.load()
for y in range(H):                       # hard edges: icons have one see-through colour
    for x in range(W):
        r_, g, b, a = px[x, y]
        px[x, y] = (r_, g, b, 255) if a >= 128 else (0, 0, 0, 0)
open(sys.argv[1], "wb").write(img.tobytes())
print(sys.argv[1], W, H)
