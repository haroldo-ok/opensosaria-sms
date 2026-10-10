#!/usr/bin/env python3
"""Colour histogram of PPM frames: colors.py FILE.ppm [...]"""
import sys
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from compare_frames import read_ppm  # noqa: E402

for path in sys.argv[1:]:
    w, h, px = read_ppm(path)
    c = Counter(tuple(px[i:i + 3]) for i in range(0, len(px), 3))
    print(path, [(f"#{r:02x}{g:02x}{b:02x}", n) for (r, g, b), n in c.most_common(8)])
