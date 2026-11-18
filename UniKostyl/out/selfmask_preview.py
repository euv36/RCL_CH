# -*- coding: utf-8 -*-
"""out/selfmask_preview.py — render the Idea 8 self-mask zone over the real
reference scene (no camera needed).

Loads UniKostyl/out/scene_red_folder.png (the user's test scene: green
Raspberry Pi PCB + black carbon tubes in the center, red folder / green
towel at the edges), downscales it to the Pi working size 640x360, builds
the default keep mask (cx=0.5, cy=0.5, r=0.22*min(w,h)) and saves a
visualization out/selfmask_preview.png:

  * pixels of the exclusion zone are darkened to 35% brightness;
  * the keep zone border is drawn in cyan;
  * the printed report estimates how much of the central PCB area is
    excluded (the PCB is roughly the central 14% x 17% box — measured on
    the reference frame, 2026-11-17).

Use it when tuning cx/cy/r/r2:  python out\\selfmask_preview.py [image]
"""
import os
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import self_mask as SM

DEFAULT_SCENE = os.path.join(HERE, "scene_red_folder.png")
OUT = os.path.join(HERE, "selfmask_preview.png")

# The working frame the Pi tracks at (rpi_elp_cam default track-size).
WORK_W, WORK_H = 640, 360
# The PCB box on the reference frame, fractions of the frame (measured on
# scene_red_folder.png: the board occupies roughly the central 1/7 x 1/6).
PCB = (0.42, 0.57, 0.43, 0.60)  # x0, x1, y0, y1 as fractions


def main():
    scene_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SCENE
    img = cv2.imread(scene_path)
    if img is None:
        raise SystemExit(f"Не удалось прочитать {scene_path}")
    work = cv2.resize(img, (WORK_W, WORK_H), interpolation=cv2.INTER_AREA)

    params = SM.resolve_params(WORK_W, WORK_H)
    keep = SM.build_keep_mask(params, WORK_W, WORK_H)

    excluded_frac = float(np.count_nonzero(keep == 0)) / keep.size
    x0, x1, y0, y1 = PCB
    bx0, bx1 = int(x0 * WORK_W), int(x1 * WORK_W)
    by0, by1 = int(y0 * WORK_H), int(y1 * WORK_H)
    pcb_keep = keep[by0:by1, bx0:bx1]
    pcb_excluded = float(np.count_nonzero(pcb_keep == 0)) / pcb_keep.size

    vis = (work.astype(np.float32) * 0.35).astype(np.uint8)
    vis[keep > 0] = work[keep > 0]
    border = cv2.Canny(keep, 50, 150)
    vis[border > 0] = (255, 255, 0)          # cyan zone border (BGR)
    cv2.circle(vis, (int(round(params["cx"])), int(round(params["cy"]))),
               2, (0, 0, 255), -1)
    cv2.imwrite(OUT, vis)

    print(f"сцена: {scene_path} -> {WORK_W}x{WORK_H}")
    print(f"self-маска: центр ({params['cx']:.0f}, {params['cy']:.0f}), "
          f"r={params['r']:.1f} px, r2={params['r2']:.1f}")
    print(f"исключено {excluded_frac * 100:.1f}% кадра "
          f"(плата Pi: {pcb_excluded * 100:.1f}% её площади)")
    print(f"визуализация: {OUT}")
    # The PCB must be (almost) fully inside the default zone — otherwise the
    # defaults do not match the scene and r/cx/cy need tuning.
    if pcb_excluded < 0.95:
        print("ВНИМАНИЕ: плата перекрыта не полностью — увеличьте --self-r "
              "или поправьте cx/cy", "WARN")
    return 0 if pcb_excluded >= 0.95 else 1


if __name__ == "__main__":
    raise SystemExit(main())
