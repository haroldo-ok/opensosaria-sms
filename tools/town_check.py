#!/usr/bin/env python3
"""Check a town/castle frame against the picture decoded from the disk.

Usage: python3 tools/town_check.py FRAME.ppm PX PY [castle]

The expected screen is the decoded picture shifted by the camera's scroll
offset, s = clamp(PX*7 + 3 - 128, 0, 24) rounded down to a multiple of 8. The player sprite area is skipped,
because the sprite is drawn over the picture.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_frames import read_ppm  # noqa: E402
import build_town as bt  # noqa: E402

RAW = "/home/user/ultima-raw/disk2"
PALETTE = os.path.join(HERE, "..", "data", "ow_palette.bin")


def palette_rgb():
    pal = open(PALETTE, "rb").read()
    out = []
    for i in range(16):
        b = pal[i]
        out.append(((b & 3) * 85, ((b >> 2) & 3) * 85, ((b >> 4) & 3) * 85))
    return out


def main(argv):
    frame, px, py = argv[1], int(argv[2]), int(argv[3])
    castle = len(argv) > 4 and argv[4] == "castle"
    pic_name = "CAS.PIC.bin" if castle else "TWN.PIC.bin"

    raw = open(os.path.join(RAW, pic_name), "rb").read()
    rgb = bt.decode_hgr(raw)                 # 280x192 engine colours
    to_index = bt.RGB_TO_INDEX
    pal = palette_rgb()

    s = min(max(px * 7 + 3 - 128, 0), 24) & ~7       # whole 8-px steps, as in town.c
    w, h, got = read_ppm(frame)
    scale = w // 256
    assert scale * 256 == w and scale * 192 == h, "unexpected frame size"

    # Sprite rectangle on the 256x192 screen (16x16 at left, top).
    left = px * 7 + 3 - 8 - s
    top = py * 7 + 3 - 8
    bad = 0
    total = 0
    for y in range(192):
        for x in range(256):
            if left <= x < left + 16 and top <= y < top + 16:
                continue
            col = rgb[y][x + s]
            want = bytes(pal[to_index[col]])
            i = ((y * scale) * w + (x * scale)) * 3
            have = bytes(got[i:i + 3])
            total += 1
            if have != want:
                bad += 1
                if bad <= 12:
                    print(f"  diff at screen ({x},{y}): frame {have.hex()} expected {want.hex()} (index {to_index[col]})")
    print(f"{frame}: scroll {s}, player ({px},{py}): {bad} of {total} pixels differ")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    sys.exit(main(sys.argv))
