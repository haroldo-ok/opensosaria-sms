#!/usr/bin/env python3
"""Count palette indices in the built SMS tile sheet (data/ow_tiles.bin)."""
from collections import Counter

d = open('data/ow_tiles.bin', 'rb').read()
counts = Counter()
for t in range(len(d) // 32):
    tile = d[t * 32:(t + 1) * 32]
    for y in range(8):
        p0, p1, p2, p3 = tile[y * 4:y * 4 + 4]
        for x in range(8):
            bit = 0x80 >> x
            c = ((p0 & bit) and 1) | ((p1 & bit) and 2) | ((p2 & bit) and 4) | ((p3 & bit) and 8)
            counts[c] += 1
print("palette index histogram over all tiles:", dict(sorted(counts.items())))
