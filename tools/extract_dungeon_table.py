#!/usr/bin/env python3
"""Decode the dungeon perspective table PE% from BEVERY, as the engine does.

Mirrors sceneDiskLoader.c: findVariableOffset("PE%") then
decodeBidimentionalArray(..., width 4, height 11) with variableHeaderCount 5.
Each value is a big-endian 16-bit integer, stored column by column (x outer,
y inner). The result is dungeonTable[11][4] = {left, right, top, bottom}.

Usage: python3 tools/extract_dungeon_table.py RAWDIR [OUT.bin]
  RAWDIR is the extractor output (contains disk2/BEVERY.bin).
  If OUT.bin is given, writes 11*4 big-endian int16 values, row by row.
"""
import os
import sys

from build_town import strip_bload

WIDTH = 4          # columns: left, right, top, bottom
HEIGHT = 11        # OS_DUNGEON_TABLE_HEIGHT
VARIABLE_HEADER_COUNT = 5


def find_variable_offset(data, name):
    """Port of sceneDiskLoader_findVariableOffset for names like 'PE%'."""
    first = second = None
    letters = 0
    for ch in name:
        if ch in "$%":
            continue
        if ch.isalpha():
            if letters == 0:
                first = ch.upper()
            elif letters == 1:
                second = ch.upper()
            letters += 1
            if letters > 2:
                return 0
        else:
            return 0
    if letters == 0:
        return 0
    b0 = ord(first)
    b1 = 0x80 if letters == 1 else (ord(second) | 0x80)
    for i in range(len(data) - 1):
        if (data[i] & 0x7F) == b0 and data[i + 1] == b1:
            return i
    return 0


def decode_array(data, index, width, height):
    """Port of sceneDiskLoader_decodeBidimentionalArray."""
    index += 4
    out = [[0] * width for _ in range(height)]
    for x in range(width):
        for y in range(height):
            out[y][x] = (data[index] << 8) | data[index + 1]
            index += 2
    return out


def main(argv):
    raw = argv[1]
    data = strip_bload(open(os.path.join(raw, "disk2", "BEVERY.bin"), "rb").read())
    address = find_variable_offset(data, "PE%")
    table = decode_array(data, address + VARIABLE_HEADER_COUNT, WIDTH, HEIGHT)
    print(f"PE% at BEVERY offset {address}")
    for row in table:
        print("  ", row)
    if len(argv) > 2:
        blob = bytearray()
        for row in table:
            for v in row:
                blob += bytes(((v >> 8) & 0xFF, v & 0xFF))
        with open(argv[2], "wb") as fh:
            fh.write(blob)
        print(f"wrote {argv[2]}: {len(blob)} bytes")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv))
