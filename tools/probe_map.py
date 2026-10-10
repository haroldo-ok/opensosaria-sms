#!/usr/bin/env python3
"""Probe the overworld map used by tests (start tile, shoreline test points)."""
import sys

d = open('data/ow_map.bin', 'rb').read()


def tile(x, y):
    r = (1 if x >= 86 else 0) + (2 if y >= 86 else 0)
    lx = x - 86 if x >= 86 else x
    ly = y - 86 if y >= 86 else y
    if lx >= 64 or ly >= 64:
        return 0
    return d[r * 4096 + ly * 64 + lx] >> 4


print("start (40,40) tile:", tile(40, 40), "east:", [tile(40 + i, 40) for i in range(1, 6)])
for y in range(10, 60):
    for x in range(10, 60):
        if tile(x, y) in (1, 2) and tile(x - 1, y) == 0 and tile(x + 1, y) in (1, 2) and tile(x + 2, y) in (1, 2):
            print("shore test point", x, y, "east tiles", tile(x + 1, y), tile(x + 2, y))
            sys.exit(0)
print("no shore point found")
