#!/usr/bin/env python3
"""Show where two PPM frames differ: bounding box and a coarse tile histogram."""
import sys
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from compare_frames import read_ppm  # noqa: E402


def main(argv):
    w, h, a = read_ppm(argv[1])
    _, _, b = read_ppm(argv[2])
    xs, ys = [], []
    cells = Counter()
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            p = i // 3
            x, y = p % w, p // w
            xs.append(x)
            ys.append(y)
            cells[(x // 8, y // 8)] += 1
    if not xs:
        print("identical")
        return 0
    print(f"differing pixels: {len(xs)}  bbox x={min(xs)}..{max(xs)} y={min(ys)}..{max(ys)}")
    print("top 8x8 cells (cell_x, cell_y): count")
    for (cx, cy), n in cells.most_common(12):
        print(f"  ({cx:2d},{cy:2d}): {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
