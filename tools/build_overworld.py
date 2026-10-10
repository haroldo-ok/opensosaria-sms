#!/usr/bin/env python3
"""Convert Ultima I overworld data (from the user's disks) into SMS assets.

Inputs (raw files from tools/extract_disks.py):
    <raw>/disk1/ULTSHAPES.bin   16 overworld tiles, Apple II HGR bit planes
    <raw>/disk2/BTERRA0..3.bin  four 64x64 realm maps (tile in high nibble)

Outputs (in data/, consumed by folder2c):
    ow_tiles.bin          64 SMS 8x8 4bpp tiles = 16 terrain tiles, 2x2 each
    ow_map.bin            4 realms x 64x64 raw map bytes (4096 each)
    ow_palette.bin        16-byte SMS BG palette (6 colours used)
    ow_sprites.bin        4 SMS 8x8 4bpp tiles for the 16x16 player sprite
    ow_sprite_palette.bin 16-byte SMS sprite palette
    ow_world_preview.png  visual check of the full 172x172 world (not built in)

Pixel colouring and gap filling reproduce sceneDiskLoader.c in open-sosaria:
    hgrColorForPixel(), fillHGRGaps(), and the 14-pixel tile layout.

Usage: python3 tools/build_overworld.py RAWDIR OUTDIR
"""
import os
import struct
import sys

from PIL import Image

TILE_W, TILE_H = 14, 16       # Apple II tile size used by the engine
TILES = 16                    # overworld terrain tiles
ATLAS_TILES_PER_ROW = 8       # engine atlas layout

# Palette indices used by the SMS overworld palette.
BLACK, WHITE, BLUE, ORANGE, GREEN, PURPLE = 0, 1, 2, 3, 4, 5

# Engine colours (sceneDiskLoader.c) and their SMS 2-bit-per-channel values.
ENGINE_RGB = {
    BLACK: (0, 0, 0), WHITE: (255, 255, 255), BLUE: (0, 146, 255),
    ORANGE: (255, 86, 0), GREEN: (36, 182, 0), PURPLE: (146, 0, 255),
}
SPRITE_WHITE, SPRITE_ORANGE, SPRITE_BLUE = 1, 2, 3


def strip_bload(data):
    """Mirror sceneDiskLoader_maybeStripBloadHeader: 2-byte addr, 2-byte length."""
    if len(data) >= 8:
        body = data[2] | (data[3] << 8)
        if body > 0 and body + 4 <= len(data):
            return data[4:4 + body]
    return data


def sms_color_byte(idx):
    r, g, b = ENGINE_RGB[idx]
    q = lambda v: round(v * 3 / 255)
    return q(r) | (q(g) << 2) | (q(b) << 4)


def hgr_pixel_colour(on, hi, parity_even, adjacent_on):
    if not on:
        return BLACK
    if adjacent_on:
        return WHITE
    if hi:
        return BLUE if parity_even else ORANGE
    return PURPLE if parity_even else GREEN


def decode_terrain_atlas(shapes):
    """Build the 112x32 atlas (8 tiles per row, 14x16 each) as in the engine."""
    tile_count = min(256, 256) // 16          # 16 tiles from two 256-byte banks
    bank_a, bank_b = 0x100, 0x000
    rows = (tile_count + ATLAS_TILES_PER_ROW - 1) // ATLAS_TILES_PER_ROW
    w = min(tile_count, ATLAS_TILES_PER_ROW) * TILE_W
    h = rows * TILE_H
    atlas = [[BLACK] * w for _ in range(h)]

    for t in range(tile_count):
        ox = (t % ATLAS_TILES_PER_ROW) * TILE_W
        oy = (t // ATLAS_TILES_PER_ROW) * TILE_H
        for y in range(TILE_H):
            b1 = shapes[bank_a + t * 16 + y]
            b2 = shapes[bank_b + t * 16 + y]
            on = [False] * 14
            hi = [False] * 14
            for i in range(7):
                on[i] = bool((b1 >> i) & 1)
                hi[i] = bool(b1 & 0x80)
                on[7 + i] = bool((b2 >> i) & 1)
                hi[7 + i] = bool(b2 & 0x80)
            for x in range(TILE_W):
                adj = (x > 0 and on[x - 1]) or (x < 13 and on[x + 1])
                atlas[oy + y][ox + x] = hgr_pixel_colour(on[x], hi[x], (x & 1) == 0, adj)

    fill_hgr_gaps(atlas, w, h)
    return atlas, w, h


def fill_hgr_gaps(atlas, w, h):
    """Port of fillHGRGaps(): black pixels between two equal lit neighbours."""
    for y in range(h):
        for x in range(1, w - 1):
            cur = atlas[y][x]
            prev = atlas[y][x - 1]
            nxt = atlas[y][x + 1]
            if cur == BLACK and prev == nxt and prev != BLACK:
                if prev != WHITE:
                    atlas[y][x] = prev
                else:
                    atlas[y][x] = GREEN if (x & 1) == 0 else PURPLE


def encode_sms_tile(pixels, x0, y0):
    """8x8 block of palette indices -> 32-byte SMS 4bpp planar tile."""
    out = bytearray()
    for y in range(8):
        planes = [0, 0, 0, 0]
        for x in range(8):
            c = pixels[y0 + y][x0 + x]
            for p in range(4):
                if (c >> p) & 1:
                    planes[p] |= 0x80 >> x
        out += bytes(planes)
    return bytes(out)


def build_terrain_tiles(atlas):
    """Each 14x16 terrain tile -> 16x16 by nearest-neighbour stretch -> 4 SMS tiles."""
    out = bytearray()
    for t in range(TILES):
        ox = (t % ATLAS_TILES_PER_ROW) * TILE_W
        oy = (t // ATLAS_TILES_PER_ROW) * TILE_H
        img = [[BLACK] * 16 for _ in range(16)]
        for y in range(16):
            for x in range(16):
                sx = (x * TILE_W) // 16
                img[y][x] = atlas[oy + y][ox + sx]
        for q in range(4):
            out += encode_sms_tile(img, (q & 1) * 8, (q >> 1) * 8)
    return bytes(out)


PLAYER_ART = [
    "................",
    ".....2222.......",
    "....22222.......",
    "....1111111.....",
    "...1111111111...",
    "...11.1111.11...",
    "....11111111....",
    "....11111111....",
    "...111111111....",
    "...111111111....",
    ".....111111.....",
    ".....11..11.....",
    ".....11..11.....",
    "....111..111....",
    "....111..111....",
    "................",
]



def build_player_sprite():
    """16x16 metasprite as four 8x8 tiles in order TL, TR, BL, BR."""
    px = [[0] * 16 for _ in range(16)]
    code = {".": 0, "1": SPRITE_WHITE, "2": SPRITE_ORANGE, "3": SPRITE_BLUE}
    for y, row in enumerate(PLAYER_ART):
        for x, ch in enumerate(row):
            px[y][x] = code[ch]
    out = bytearray()
    for q in range(4):
        out += encode_sms_tile(px, (q & 1) * 8, (q >> 1) * 8)
    return bytes(out)


def world_tile_preview(realms, path):
    """172x172 world, one pixel per tile, using the palette colours."""
    size = 172
    img = Image.new("RGB", (size, size), (0, 0, 0))
    px = img.load()
    palette = {BLACK: (0, 0, 0), BLUE: (0, 146, 255), ORANGE: (255, 86, 0),
               GREEN: (36, 182, 0), PURPLE: (146, 0, 255), WHITE: (255, 255, 255)}
    # Terrain colour for each tile id (0 water .. 15).
    terrain = {0: (0, 90, 220), 1: (70, 160, 40), 2: (30, 110, 30), 3: (150, 150, 150)}
    for ty in range(size):
        for tx in range(size):
            realm = (ty // 86) * 2 + (tx // 86)
            lx, ly = tx % 86, ty % 86
            if lx >= 64 or ly >= 64:
                tile = 0
                tt = 0
            else:
                b = realms[realm][ly * 64 + lx]
                tile, tt = (b >> 4) & 0x0F, b & 0x0F
            if tile in terrain:
                c = terrain[tile]
            elif tile in (4, 5, 6, 7):
                c = (255, 220, 0)
            else:
                c = (120, 120, 120)
            px[tx, ty] = c
    img.resize((size * 2, size * 2), Image.NEAREST).save(path)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    raw, out = argv[1], argv[2]
    os.makedirs(out, exist_ok=True)

    shapes = strip_bload(open(os.path.join(raw, "disk1", "ULTSHAPES.bin"), "rb").read())
    realms = []
    for i in range(4):
        data = strip_bload(open(os.path.join(raw, "disk2", f"BTERRA{i}.bin"), "rb").read())
        if len(data) < 64 * 64:
            raise SystemExit(f"BTERRA{i} too short: {len(data)}")
        realms.append(bytes(data[:4096]))

    atlas, _, _ = decode_terrain_atlas(shapes)
    tiles = build_terrain_tiles(atlas)
    assert len(tiles) == 64 * 32

    bg_palette = bytearray(16)
    for idx in range(6):
        bg_palette[idx] = sms_color_byte(idx)
    sprite_palette = bytearray(16)
    sprite_palette[SPRITE_WHITE] = sms_color_byte(WHITE)
    sprite_palette[SPRITE_ORANGE] = sms_color_byte(ORANGE)
    sprite_palette[SPRITE_BLUE] = sms_color_byte(BLUE)

    files = {
        "ow_tiles.bin": tiles,
        "ow_palette.bin": bytes(bg_palette),
        "ow_sprites.bin": build_player_sprite(),
        "ow_sprite_palette.bin": bytes(sprite_palette),
        # Map realms go to ROM banks (see PORTING_PLAN.md). Realms 0-1 -> bank 2,
        # realms 2-3 -> bank 3. Kept out of data/ so folder2c leaves them out.
        "banked/bank2.bin": b"".join(realms[0:2]),
        "banked/bank3.bin": b"".join(realms[2:4]),
    }
    os.makedirs(os.path.join(out, "banked"), exist_ok=True)
    for name, blob in files.items():
        with open(os.path.join(out, name), "wb") as fh:
            fh.write(blob)
        print(f"{name}: {len(blob)} bytes")

    world_tile_preview(realms, os.path.join(out, "..", "shots", "world_preview.png"))
    print("world preview -> shots/world_preview.png")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
