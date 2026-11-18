#!/usr/bin/env python3
"""Профилирование пайплайна по стадиям (запускать НА Pi, камера должна быть свободна)."""
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, "/home/twox")
import rpi_elp_cam as R

COLORS = [(255, 255, 0), (0, 0, 255), (255, 128, 0)]
tol_text = sys.argv[1] if len(sys.argv) > 1 else "40,100,100"
tolerance = tuple(int(x) for x in tol_text.split(","))

R.apply_camera_controls("/dev/video0", 300, 0, 0)
cam = R.RawCamera("/dev/video0", 1920, 1080, 30)
W, H = 640, 360
flag = R.choose_decode_flag(1920, 1080, W, H)
tracker = R.GateTracker(W, H, tolerance, 400, 1.0, 3)
for i, c in enumerate(COLORS):
    tracker.update_gate_from_rgb(i, c)

N = 60
t_dec = 0.0
t_hsv = 0.0
t_gate = [0.0, 0.0, 0.0]
t_draw = [0.0, 0.0, 0.0]
areas = [[], [], []]
found = [0, 0, 0]
n = 0

for _ in range(N):
    ok, data = cam.read()
    t0 = time.perf_counter()
    if not ok or data is None:
        continue
    frame = R.frame_from_raw(data, 1920, W, H, flag)
    t1 = time.perf_counter()
    if frame is None:
        continue
    cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
    t2 = time.perf_counter()
    t_dec += t1 - t0
    t_hsv += t2 - t1
    n += 1

    for g in range(3):
        ta = time.perf_counter()
        info = tracker.process(g)
        tb = time.perf_counter()
        if info is not None:
            tracker.draw(frame, g, info, (0, 255, 0))
            areas[g].append(info["area_px"])
            found[g] += 1
        tc = time.perf_counter()
        t_gate[g] += tb - ta
        t_draw[g] += tc - tb

total = t_dec + t_hsv + sum(t_gate) + sum(t_draw)
print(f"кадров {n}: декод+resize {t_dec/n*1000:.1f} мс, hsv {t_hsv/n*1000:.1f} мс", flush=True)
for g in range(3):
    a = np.mean(areas[g]) if areas[g] else 0
    print(f"  gate{g+1}: process {t_gate[g]/n*1000:.1f} мс, draw {t_draw[g]/n*1000:.1f} мс, "
          f"mask_px≈{a:.0f}, найдено {found[g]}/{n}", flush=True)
print(f"сумма {total/n*1000:.1f} мс/кадр -> {n/total:.1f} fps", flush=True)
cam.release()
