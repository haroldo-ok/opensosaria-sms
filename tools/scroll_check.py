#!/usr/bin/env python3
"""Find the (dx, dy) pixel shift that best maps frame A onto frame B (excluding sprite area)."""
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from compare_frames import read_ppm

def best_shift(a, b, w, h, rng=96):
    best = None
    for dy in range(-rng, rng + 1, 8):
        for dx in range(-rng, rng + 1, 8):
            # compare a region away from the centre sprite
            score = 0; n = 0
            for y in range(40, h - 40, 4):
                ys = y + dy
                if not (0 <= ys < h): continue
                for x in range(40, w - 40, 4):
                    xs = x + dx
                    if not (0 <= xs < w): continue
                    i = (y * w + x) * 3; j = (ys * w + xs) * 3
                    n += 1
                    if a[i:i+3] == b[j:j+3]: score += 1
            if n:
                frac = score / n
                if best is None or frac > best[0]:
                    best = (frac, dx, dy)
    return best

a_name, b_name = sys.argv[1], sys.argv[2]
w, h, a = read_ppm(a_name)
_, _, b = read_ppm(b_name)
frac, dx, dy = best_shift(a, b, w, h)
print(f"{a_name} -> {b_name}: best match {frac:.2f} when content moves dx={dx} dy={dy}")
