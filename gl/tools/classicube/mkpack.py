#!/usr/bin/env python3
"""Generate a clean-room test texture pack for ClassiCube (default.zip).

ClassiCube's real textures are Minecraft Classic's and are not ours to
ship, so Loop A and the bench run with this procedural pack instead: a
256x256 terrain.png atlas (16x16 tiles) with a distinct pattern per tile,
binary alpha on the cut-out tiles (leaves, glass, saplings and flowers) and
translucent water, plus a 128x128 default.png font of simple glyphs. It
exercises the same texture formats (RGB565, ARGB1555, ARGB4444) as the real
pack. Standard library only.

  mkpack.py OUT.zip
"""
import random
import struct
import sys
import zipfile
import zlib


def png_rgba(w, h, px):
    """px: bytes of w*h RGBA."""
    raw = b"".join(b"\0" + px[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# Tile -> (base colour, alpha kind). Indices follow ClassiCube's Block.c.
TILES = {
    0: ((90, 170, 60), "opaque"),      # grass top
    1: ((128, 128, 128), "opaque"),    # stone
    2: ((120, 85, 55), "opaque"),      # dirt
    3: ((120, 85, 55), "grass_side"),  # grass side
    4: ((170, 130, 80), "opaque"),     # planks
    14: ((40, 90, 200), "water"),      # water
    15: ((60, 150, 50), "sprite"),     # sapling
    16: ((110, 110, 110), "opaque"),   # cobblestone
    17: ((50, 50, 50), "opaque"),      # bedrock
    18: ((220, 210, 150), "opaque"),   # sand
    19: ((140, 130, 125), "opaque"),   # gravel
    20: ((110, 80, 45), "opaque"),     # log side
    21: ((160, 125, 80), "opaque"),    # log top
    22: ((50, 140, 40), "leaves"),     # leaves
    30: ((230, 100, 20), "opaque"),    # lava
    49: ((200, 230, 240), "glass"),    # glass
}


def terrain():
    rnd = random.Random(1)
    w = h = 256
    px = bytearray(w * h * 4)
    for tile in range(256):
        tx, ty = (tile % 16) * 16, (tile // 16) * 16
        base, kind = TILES.get(tile, ((60 + (tile * 37) % 180, 60 + (tile * 91) % 180, 60 + (tile * 53) % 180), "opaque"))
        for y in range(16):
            for x in range(16):
                n = rnd.randint(-18, 18)
                r, g, b = (max(0, min(255, c + n)) for c in base)
                a = 255
                if kind == "grass_side" and y < 4:
                    r, g, b = 90 + n, 170 + n, 60 + n
                elif kind == "water":
                    a = 150
                elif kind == "leaves":
                    a = 255 if (x * 7 + y * 3 + tile) % 5 else 0
                elif kind == "glass":
                    a = 255 if x in (0, 15) or y in (0, 15) or x == y else 0
                elif kind == "sprite":
                    a = 255 if abs(x - 8) < (16 - y) // 3 + 1 and y > 2 else 0
                o = ((ty + y) * w + tx + x) * 4
                px[o:o + 4] = bytes((max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)), a))
    return png_rgba(w, h, bytes(px))


def font():
    """16x16 glyphs of 8x8: each printable character a distinct 5x7 dot
    pattern (not readable text; widths vary so the layout code is exercised)."""
    w = h = 128
    px = bytearray(w * h * 4)
    for ch in range(33, 127):
        gx, gy = (ch % 16) * 8, (ch // 16) * 8
        width = 3 + ch % 3
        bits = (ch * 2654435761) & 0xFFFFFFFF
        for y in range(7):
            for x in range(width):
                if (bits >> ((y * 5 + x) % 32)) & 1 or y == 6:
                    o = ((gy + y) * w + gx + x) * 4
                    px[o:o + 4] = b"\xff\xff\xff\xff"
    return png_rgba(w, h, bytes(px))


def main():
    out = sys.argv[1]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("terrain.png", terrain())
        z.writestr("default.png", font())
    print("mkpack: %s" % out)


if __name__ == "__main__":
    main()
