#!/usr/bin/env python3
"""Print terrain tile ids down a world column: probe_col.py X Y0 Y1."""
import sys

d = open('data/ow_map.bin', 'rb').read()


def tile(x, y):
    r = (1 if x >= 86 else 0) + (2 if y >= 86 else 0)
    lx = x - 86 if x >= 86 else x
    ly = y - 86 if y >= 86 else y
    if lx >= 64 or ly >= 64:
        return 0
    return d[r * 4096 + ly * 64 + lx] >> 4


x, y0, y1 = map(int, sys.argv[1:4])
print(f"column x={x} y{y0}..{y1}:", [tile(x, y) for y in range(y0, y1 + 1)])
