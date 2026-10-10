#!/usr/bin/env python3
"""Compare emulator screenshots (P6 PPM) pixel by pixel.

Used by the playtest checks: a round trip that returns the player to the same
world tile must produce a pixel-identical frame, which proves the scrolling
ring buffer and the streamed tile updates stay consistent.

Usage: python3 tools/compare_frames.py A.ppm B.ppm [...]   (all compared to A)
"""
import sys


def read_ppm(path):
    with open(path, "rb") as fh:
        data = fh.read()
    parts = data.split(maxsplit=4)
    if parts[0] != b"P6":
        raise SystemExit(f"{path}: not a P6 PPM")
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    ref_w, ref_h, ref = read_ppm(argv[1])
    ok = True
    for path in argv[2:]:
        w, h, px = read_ppm(path)
        if (w, h) != (ref_w, ref_h):
            print(f"{path}: size {w}x{h} != {ref_w}x{ref_h}")
            ok = False
            continue
        diff = sum(1 for i in range(0, len(ref), 3) if ref[i:i + 3] != px[i:i + 3])
        status = "IDENTICAL" if diff == 0 else f"{diff} pixels differ"
        print(f"{argv[1]} vs {path}: {status}")
        ok = ok and diff == 0
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
