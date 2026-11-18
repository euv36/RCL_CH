#!/usr/bin/env python3
"""Разбор по стадиям: декод MJPG разными флагами + внутренности GateTracker.process."""
import subprocess
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, "/home/twox")
import rpi_elp_cam as R


def clock():
    try:
        out = subprocess.run(["vcgencmd", "measure_clock", "arm"],
                             capture_output=True, text=True).stdout
        return int(out.strip().split("=")[1]) / 1e6
    except Exception:
        return -1.0


R.apply_camera_controls("/dev/video0", 300, 0, 0)
cam = R.RawCamera("/dev/video0", 1920, 1080, 30)

bufs = []
for _ in range(12):
    ok, data = cam.read()
    if ok and data is not None:
        bufs.append(data)
print(f"собрано {len(bufs)} кадров, размер JPEG ~{np.mean([len(b) for b in bufs])/1024:.0f} КБ",
      flush=True)

for name, flag in (("REDUCED_2", cv2.IMREAD_REDUCED_COLOR_2),
                   ("REDUCED_4", cv2.IMREAD_REDUCED_COLOR_4)):
    t0 = time.perf_counter()
    for b in bufs:
        cv2.imdecode(b, flag)
    dt = (time.perf_counter() - t0) / len(bufs)
    print(f"imdecode {name}: {dt*1000:.1f} мс", flush=True)

frame = cv2.imdecode(bufs[0], cv2.IMREAD_REDUCED_COLOR_2)
t0 = time.perf_counter()
for _ in range(len(bufs)):
    cv2.resize(frame, (640, 360), interpolation=cv2.INTER_AREA)
dt = (time.perf_counter() - t0) / len(bufs)
print(f"resize 960x540 -> 640x360: {dt*1000:.1f} мс", flush=True)

small = cv2.resize(frame, (640, 360), interpolation=cv2.INTER_AREA)
hsv = np.empty((360, 640, 3), np.uint8)
cv2.cvtColor(small, cv2.COLOR_BGR2HSV, dst=hsv)

tracker = R.GateTracker(640, 360, (40, 100, 100), 400, 1.0, 3)
for i, c in enumerate([(255, 255, 0), (0, 0, 255), (255, 128, 0)]):
    tracker.update_gate_from_rgb(i, c)

mask = np.empty((360, 640), np.uint8)
tmp = np.empty((360, 640), np.uint8)
lows = tracker.lowers[0]
ups = tracker.uppers[0]

t0 = time.perf_counter()
for _ in range(20):
    for i, lo in enumerate(lows):
        cv2.inRange(hsv, lo, ups[i], dst=tmp if i == 0 else mask)
        if i:
            cv2.bitwise_or(mask, tmp, dst=mask)
        else:
            mask, tmp = tmp, mask
dt = (time.perf_counter() - t0) / 20
print(f"inRange (все H-диапазоны): {dt*1000:.2f} мс", flush=True)

k = R.MORPH_KERNEL
t0 = time.perf_counter()
for _ in range(20):
    cv2.morphologyEx(mask, cv2.MORPH_OPEN, k, dst=tmp)
    cv2.morphologyEx(tmp, cv2.MORPH_CLOSE, k, dst=mask)
dt = (time.perf_counter() - t0) / 20
print(f"morph open+close: {dt*1000:.2f} мс", flush=True)

t0 = time.perf_counter()
for _ in range(20):
    cv2.moments(mask, binaryImage=True)
dt = (time.perf_counter() - t0) / 20
print(f"moments: {dt*1000:.2f} мс", flush=True)

print("mask пикселей:", int(cv2.countNonZero(mask)), flush=True)
print(f"частота сейчас {clock():.0f} МГц", flush=True)
cam.release()

t0 = time.perf_counter()
x = 0.0
while time.perf_counter() - t0 < 2.0:
    x += sum(range(1000))
print(f"CPU-бенч за 2 c: {x:.0f}, частота после {clock():.0f} МГц", flush=True)
