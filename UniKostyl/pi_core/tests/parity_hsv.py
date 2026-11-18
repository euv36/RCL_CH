#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
parity_hsv.py — generates RGB->HSV test vectors with THIS machine's real cv2.
Run it on the Pi (OpenCV 4.10) and feed the output to pi_core_tests:

    cd /home/twox/pi_core
    python3 tests/parity_hsv.py > /tmp/hsv_vectors.txt
    ./build/pi_core_tests --vectors /tmp/hsv_vectors.txt

The vectors cover the primaries/secondaries/grays, gate-like colors
(yellow/blue/orange) and a deterministic random sweep. Exit code is 0 even
when cv2 is missing? No — it requires cv2 (that's the point of the parity).
"""
import sys

import cv2
import numpy as np

random = __import__("random")
random.seed(1234)


def hsv_of(r, g, b):
    out = cv2.cvtColor(np.uint8([[[r, g, b]]]), cv2.COLOR_RGB2HSV)[0, 0]
    return int(out[0]), int(out[1]), int(out[2])


def main():
    pts = [
        (0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255),
        (255, 255, 0), (0, 255, 255), (255, 0, 255), (128, 128, 128),
        (1, 1, 1), (254, 254, 254), (255, 254, 253), (0, 0, 1), (1, 0, 0),
        (180, 40, 20), (60, 120, 240), (255, 128, 64), (34, 17, 68),
        (200, 100, 150), (17, 255, 90), (70, 252, 46), (29, 41, 95),
        # gate-like colors (yellow / blue / orange families)
        (255, 230, 0), (230, 255, 0), (0, 90, 255), (40, 60, 255),
        (255, 140, 0), (255, 90, 40), (250, 180, 30),
    ]
    for _ in range(2000):
        pts.append(
            (random.randint(0, 255), random.randint(0, 255), random.randint(0, 255)))

    out = sys.stdout
    for (r, g, b) in pts:
        h, s, v = hsv_of(r, g, b)
        out.write(f"{r} {g} {b} {h} {s} {v}\n")
    print(f"# {len(pts)} vectors via cv2 {cv2.__version__}", file=sys.stderr)


if __name__ == "__main__":
    main()
