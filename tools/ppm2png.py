#!/usr/bin/env python3
"""Convert a P6 PPM screenshot to PNG (2x) for viewing: ppm2png.py IN.ppm OUT.png"""
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from compare_frames import read_ppm  # noqa: E402

w, h, px = read_ppm(sys.argv[1])
Image.frombytes("RGB", (w, h), px).resize((w * 2, h * 2), Image.NEAREST).save(sys.argv[2])
print(f"{sys.argv[1]} -> {sys.argv[2]}")
