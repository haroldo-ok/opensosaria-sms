#!/usr/bin/env python3
"""Convert Ultima I town and castle screens and collision maps into SMS banks.

Inputs (raw files from tools/extract_disks.py):
    <raw>/disk2/TWN.PIC.bin, CAS.PIC.bin   280x192 Apple II HGR pictures (8192 B)
    <raw>/disk2/BTWN.bin, BCAS.bin         40x22 collision maps (BLOAD, 1024 B)

Outputs (in data/banked/, one ROM bank each; see PORTING_PLAN.md):
    bank4.bin         town picture tiles + tilemap + collision
    bank5.bin         castle picture tiles + tilemap + collision

Bank layout (all offsets from the start of the bank):
    0                  2 bytes        N, unique tile count, little-endian
    2                  N * 32 bytes   unique 8x8 4bpp tiles (N <= TILE_LIMIT)
    2 + N * 32         35 * 24 * 2    tilemap, row-major, 16-bit little-endian word per
                                      cell, tile index relative to the screen's tile base
    2 + N * 32 + 1680  40 * 22 bytes  collision, column-major: index = x * 22 + y

The HGR decode reproduces sceneDiskLoader_decodeHGRImage and fillHGRGaps in
open-sosaria. Colours map to the same six BG palette indices as the overworld
(see build_overworld.py), so ow_palette.bin applies to these screens.

Usage: python3 tools/build_town.py RAWDIR OUTDIR
"""
import os
import sys

WIDTH, HEIGHT = 280, 192          # HGR picture size (engine OS_SCREEN_WIDTH/HEIGHT)
TILE = 8
COLS, ROWS = WIDTH // TILE, HEIGHT // TILE     # 35 x 24
MAP_W, MAP_H = 40, 22             # collision grid (config.h)
TILE_LIMIT = 288                  # VRAM slots 96..383 (see PORTING_PLAN.md)

# Palette indices (same as build_overworld.py)
BLACK, WHITE, BLUE, ORANGE, GREEN, PURPLE = 0, 1, 2, 3, 4, 5
# Engine colours, sceneDiskLoader.c
RGB_TO_INDEX = {
    (0, 0, 0): BLACK,
    (255, 255, 255): WHITE,
    (0, 146, 255): BLUE,
    (255, 86, 0): ORANGE,
    (36, 182, 0): GREEN,
    (146, 0, 255): PURPLE,
}
COL_BLACK = (0, 0, 0)
COL_WHITE = (255, 255, 255)
COL_GREEN = (36, 182, 0)
COL_PURPLE = (146, 0, 255)
COL_BLUE = (0, 146, 255)
COL_ORANGE = (255, 86, 0)


def strip_bload(data):
    """Mirror sceneDiskLoader_maybeStripBloadHeader: 2-byte addr, 2-byte length."""
    if len(data) >= 8:
        body = data[2] | (data[3] << 8)
        if body > 0 and body + 4 <= len(data):
            return data[4:4 + body]
    return data


def decode_hgr(raw):
    """Decode 280x192 HGR to a list of rows of RGB tuples, engine rules."""
    data = strip_bload(raw)
    if len(data) < 8184:
        raise ValueError("HGR picture too short: %d bytes" % len(data))

    img = [[COL_BLACK] * WIDTH for _ in range(HEIGHT)]
    for y in range(HEIGHT):
        row_base = ((y & 7) * 0x400) + (((y >> 3) & 7) * 0x80) + ((y >> 6) * 0x28)
        bits = [0] * WIDTH
        phase = [0] * WIDTH
        for xb in range(40):
            addr = row_base + xb
            if addr >= len(data):
                continue
            byte = data[addr]
            ph = (byte >> 7) & 1
            for bit in range(7):
                x = xb * 7 + bit
                bits[x] = (byte >> bit) & 1
                phase[x] = ph
        for x in range(WIDTH):
            left = bits[x - 1] if x > 0 else 0
            current = bits[x]
            right = bits[x + 1] if x < WIDTH - 1 else 0
            if not current:
                c = COL_BLACK
            elif left or right:
                c = COL_WHITE
            else:
                odd = x & 1
                if phase[x] == 0:
                    c = COL_GREEN if odd else COL_PURPLE
                else:
                    c = COL_ORANGE if odd else COL_BLUE
            img[y][x] = c
    fill_gaps(img)
    return img


def fill_gaps(img):
    """In-place port of fillHGRGaps: runs left to right, so later pixels see earlier fills."""
    for y in range(HEIGHT):
        row = img[y]
        for x in range(1, WIDTH - 1):
            prev, nxt = row[x - 1], row[x + 1]
            is_curr_black = row[x] == COL_BLACK
            neighbors_same = prev == nxt
            neighbor_lit = prev != COL_BLACK
            is_neighbor_white = prev == COL_WHITE
            if is_curr_black and neighbors_same and neighbor_lit:
                if not is_neighbor_white:
                    row[x] = prev
                elif (x & 1) == 0:
                    row[x] = COL_GREEN
                else:
                    row[x] = COL_PURPLE


def to_indices(img):
    out = []
    for row in img:
        try:
            out.append([RGB_TO_INDEX[c] for c in row])
        except KeyError as exc:
            raise ValueError("unexpected colour %r" % (exc.args[0],))
    return out


def encode_tile(idx, x0, y0):
    """8x8 SMS 4bpp tile: 4 bytes per row, plane 0 first, MSB = leftmost pixel."""
    out = bytearray()
    for y in range(TILE):
        planes = [0, 0, 0, 0]
        for x in range(TILE):
            c = idx[y0 + y][x0 + x]
            for p in range(4):
                if (c >> p) & 1:
                    planes[p] |= 0x80 >> x
        out.extend(planes)
    return bytes(out)


def build_picture(raw):
    """Return (tile_bytes, tilemap_bytes, unique_count)."""
    idx = to_indices(decode_hgr(raw))
    tiles = {}
    order = []
    tilemap = bytearray()
    for r in range(ROWS):
        for c in range(COLS):
            t = encode_tile(idx, c * TILE, r * TILE)
            if t not in tiles:
                tiles[t] = len(order)
                order.append(t)
            tilemap += bytes((tiles[t] & 0xFF, tiles[t] >> 8))
    if len(order) > TILE_LIMIT:
        raise ValueError("%d unique tiles exceeds %d" % (len(order), TILE_LIMIT))
    return b"".join(order), bytes(tilemap), len(order)


def build_collision(raw):
    data = strip_bload(raw)
    need = MAP_W * MAP_H
    if len(data) < need:
        raise ValueError("collision map too short: %d" % len(data))
    # sceneDiskLoader_loadCollisionsMap copies x-major (outer x, inner y).
    return bytes(data[:need])


def build_bank(pic_raw, col_raw):
    tiles, tilemap, n = build_picture(pic_raw)
    collision = build_collision(col_raw)
    header = bytes((n & 0xFF, n >> 8))
    return header + tiles + tilemap + collision, n


def main(argv):
    raw, out = argv[1], argv[2]
    os.makedirs(os.path.join(out, "banked"), exist_ok=True)
    jobs = [
        ("TWN.PIC.bin", "BTWN.bin", "banked/bank4.bin"),
        ("CAS.PIC.bin", "BCAS.bin", "banked/bank5.bin"),
    ]
    for pic, col, name in jobs:
        pic_raw = open(os.path.join(raw, "disk2", pic), "rb").read()
        col_raw = open(os.path.join(raw, "disk2", col), "rb").read()
        blob, n = build_bank(pic_raw, col_raw)
        with open(os.path.join(out, name), "wb") as fh:
            fh.write(blob)
        print(f"{name}: {len(blob)} bytes, {n} unique tiles")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv))
