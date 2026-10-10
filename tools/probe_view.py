#!/usr/bin/env python3
"""Histogram of terrain ids in the 16x12 view around a world tile: probe_view.py X Y"""
import sys
from collections import Counter

d = open('data/ow_map.bin', 'rb').read()


def tile(x, y):
    r = (1 if x >= 86 else 0) + (2 if y >= 86 else 0)
    lx = x - 86 if x >= 86 else x
    ly = y - 86 if y >= 86 else y
    if lx >= 64 or ly >= 64:
        return 0
    return d[r * 4096 + ly * 64 + lx] >> 4


px, py = int(sys.argv[1]), int(sys.argv[2])
cx, cy = px - 8, py - 6
h = Counter(tile(cx + c, cy + r) for r in range(12) for c in range(16))
print("view terrain histogram (tile id: count):", dict(sorted(h.items())))
