#!/usr/bin/env python3
"""List overworld tiles of one terrain kind (high nibble) from the ROM map banks.

Usage: python3 tools/find_places.py TILE [limit]
  TILE 4 = castle, 5 = signpost, 6 = town, 7 = dungeon.

Reads data/banked/bank2.bin (realms 0-1) and bank3.bin (realms 2-3). Realm r
starts at x = 86 * (r & 1) and y = 86 * (r >> 1), per engine rules.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BANKED = os.path.join(HERE, "..", "data", "banked")


def main(argv):
    want = int(argv[1])
    limit = int(argv[2]) if len(argv) > 2 else 20
    blobs = {
        0: open(os.path.join(BANKED, "bank2.bin"), "rb").read()[0:4096],
        1: open(os.path.join(BANKED, "bank2.bin"), "rb").read()[4096:8192],
        2: open(os.path.join(BANKED, "bank3.bin"), "rb").read()[0:4096],
        3: open(os.path.join(BANKED, "bank3.bin"), "rb").read()[4096:8192],
    }
    found = []
    for realm, data in blobs.items():
        ox = 86 * (realm & 1)
        oy = 86 * (realm >> 1)
        for ly in range(64):
            for lx in range(64):
                b = data[ly * 64 + lx]
                if (b >> 4) == want:
                    found.append((ox + lx, oy + ly, realm, b & 0x0F))
    print(f"tile {want}: {len(found)} found")
    for x, y, realm, kind in found[:limit]:
        print(f"  world ({x},{y}) realm {realm} type {kind}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv))
