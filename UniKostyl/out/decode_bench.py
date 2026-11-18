#!/usr/bin/env python3
"""Сколько стоит декод свежих кадров при разных флагах + размер JPEG и яркость сцены."""
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, "/home/twox")
import rpi_elp_cam as R

R.apply_camera_controls("/dev/video0", 300, 0, 0)
cam = R.RawCamera("/dev/video0", 1920, 1080, 30)

sizes = []
luma = []
bufs = []
for _ in range(25):
    ok, data = cam.read()
    if not ok or data is None:
        continue
    raw = bytes(np.asarray(data).ravel())
    sizes.append(len(raw))
    bufs.append(data)
    img = cv2.imdecode(data, cv2.IMREAD_REDUCED_COLOR_4)
    if img is not None:
        luma.append(float(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).mean()))

print(f"JPEG: {np.mean(sizes)/1024:.0f} КБ (min {min(sizes)/1024:.0f}, max {max(sizes)/1024:.0f}), "
      f"luma≈{np.mean(luma):.1f}", flush=True)

for name, flag in (("COLOR (1080p)", cv2.IMREAD_COLOR),
                   ("REDUCED_2 (960)", cv2.IMREAD_REDUCED_COLOR_2),
                   ("REDUCED_4 (480)", cv2.IMREAD_REDUCED_COLOR_4)):
    for cached in (False, True):
        total = 0.0
        n = 0
        for i in range(20):
            if cached:
                data = bufs[i % len(bufs)]
            else:
                ok, data = cam.read()
                if not ok:
                    continue
            t0 = time.perf_counter()
            f = cv2.imdecode(data, flag)
            total += time.perf_counter() - t0
            n += 1
        mode = "кэш" if cached else "свежие"
        print(f"{name:16s} {mode:7s}: {total/n*1000:5.1f} мс  -> {f.shape[1]}x{f.shape[0]}",
              flush=True)

cam.release()
