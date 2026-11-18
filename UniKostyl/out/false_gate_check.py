# -*- coding: utf-8 -*-
"""Camera-free regression: one color must yield at most one gate-like object."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import cv2
import numpy as np

import gate_cluster

H, W = 1280, 720

def mask_with(shapes):
    m = np.zeros((H, W), np.uint8)
    for x, y, w, h in shapes:
        cv2.rectangle(m, (x, y), (x + w - 1, y + h - 1), 255, -1)
    return m

def run_case(name, shapes, expect_centroid=None, expect_found=True):
    params = gate_cluster.resolve_params(W, H, min_area_px=100.0)
    det = gate_cluster.detect_gate(mask_with(shapes), params)
    print(f"[{name}] det={det}")
    if not expect_found:
        assert det is None, f"{name}: expected None, got {det}"
        return
    assert det is not None, f"{name}: expected gate, got None"
    if expect_centroid is not None:
        ex, ey = expect_centroid
        assert abs(det["cx"] - ex) < 35, f"{name}: cx {det['cx']:.1f} != {ex}"
        assert abs(det["cy"] - ey) < 35, f"{name}: cy {det['cy']:.1f} != {ey}"
    assert det["area"] > 0

def main():
    folder = [(420, 520, 190, 150)]
    folder_cx = 420 + 189 / 2
    folder_cy = 520 + 149 / 2
    tape = [(120, 880, 360, 11)]
    lamp = [(40, 40, 80, 700)]

    run_case("folder alone", folder, (folder_cx, folder_cy))
    run_case("folder + tape", folder + tape, (folder_cx, folder_cy))
    run_case("folder + lamp", folder + lamp, (folder_cx, folder_cy))
    run_case("folder + lamp + tape", folder + lamp + tape, (folder_cx, folder_cy))
    run_case("same colour tape only", tape, expect_found=False)
    run_case("same colour lamp only", lamp, expect_found=False)
    run_case("edge partially visible gate", [(0, 500, 105, 170)], (52, 584))
    split = [(300, 520, 85, 150), (398, 520, 85, 150)]  # 13 px gap
    run_case("split goal merges", split, (391, 594))
    print("false_gate_check: OK")

if __name__ == "__main__":
    main()
