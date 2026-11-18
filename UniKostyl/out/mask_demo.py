"""Demo: per-pixel masking on real frames; picks the frame with most detections."""
import os

import cv2
import numpy as np

import rpi_elp_cam as R

BASE = r"D:\camera\UniKostyl\out"
cands = ["gate_288.0_raw.jpg", "gate_162.0_raw.jpg", "panorama.jpg",
         r"fov_new\fov_new_full.jpg", r"fov_new\fov_new.jpg"]
colors = [(255, 255, 0), (0, 0, 255), (255, 128, 0)]
overlay = ((0, 255, 0), (255, 0, 255), (0, 255, 255))

best = None
for name in cands:
    p = os.path.join(BASE, name)
    if not os.path.exists(p):
        continue
    img = cv2.imread(p)
    if img is None:
        continue
    img = cv2.resize(img, (640, 360), interpolation=cv2.INTER_AREA)
    tr = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
    for i, c in enumerate(colors):
        tr.update_gate_from_rgb(i, c)
    cv2.cvtColor(img, cv2.COLOR_BGR2HSV, dst=tr.hsv)
    out = img.copy()
    hits = []
    for g in range(3):
        info = tr.process(g)
        if info is not None:
            tr.draw(out, g, info, overlay[g])
            hits.append((g + 1, info["angle"], int(info["area_px"])))
    print(f"{name}: {hits}")
    if best is None or len(hits) > len(best[1]):
        best = (img, hits, out)

if best is None:
    print("no candidate frames found")
else:
    img, hits, out = best
    panel = np.hstack([img, out])
    cv2.putText(panel, "original", (8, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(panel, "per-pixel mask", (648, 20), cv2.FONT_HERSHEY_SIMPLEX,
                0.6, (255, 255, 255), 2, cv2.LINE_AA)
    dst = os.path.join(BASE, "mask_demo.jpg")
    cv2.imwrite(dst, panel, [int(cv2.IMWRITE_JPEG_QUALITY), 90])
    print("saved", dst, "hits", hits)
