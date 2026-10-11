#!/usr/bin/env python3
"""Check the frames saved by tests/walk_frames.txt (see that file).

Usage: python3 tools/check_walk_frames.py shots/walk
Exits 1 if any frame jumps or the console shows non-text colours.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

MAP_H = 160                              # map rows; the console is below
TEXT = {(0, 0, 0), (255, 255, 255)}


def main(folder):
    files = sorted(glob.glob(os.path.join(folder, "*.ppm")))
    if not files:
        print("no frames in", folder)
        return 1
    mask = np.ones((MAP_H, 256), bool)   # ignore the edges and the player sprite
    mask[:16] = mask[-16:] = False
    mask[:, :16] = mask[:, -16:] = False
    mask[72:96, 120:152] = False
    jumps, dirty = [], []
    prev, prev_prefix = None, None
    for f in files:
        name = os.path.basename(f)
        img = np.asarray(Image.open(f).convert("RGB")).astype(int)
        top = img[:MAP_H]
        prefix = name[0]
        if prev is not None and prefix == prev_prefix:
            ok = any(
                (np.abs(top - np.roll(prev, (-dy, -dx), (0, 1))).sum(2)[mask] == 0).mean() > 0.98
                for dx, dy in [(0, 0), (16, 0), (-16, 0), (0, 16), (0, -16)])
            if not ok:
                jumps.append(name)
        colours = {tuple(c) for c in np.unique(img[MAP_H:].reshape(-1, 3), axis=0)}
        if not colours <= TEXT:
            dirty.append(name)
        prev, prev_prefix = top, prefix
    print("walk frames: %d, map jumps: %d %s, console not text: %d %s"
          % (len(files), len(jumps), jumps[:8], len(dirty), dirty[:8]))
    return 1 if jumps or dirty else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "shots/walk"))
