#!/usr/bin/env python3
"""Measure raw MJPEG capture and decode-at-scale throughput on the Pi."""
import cv2, time, numpy as np, sys

DEV = "/dev/video0"

def open_cam(w, h, fps=30, convert_rgb=False):
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    if not cap.isOpened():
        raise RuntimeError("cannot open camera")
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 1 if convert_rgb else 0)
    return cap

# 1) Raw capture: what does read() return with CONVERT_RGB=0?
cap = open_cam(1920, 1080, 30, convert_rgb=False)
for _ in range(10):
    ok, raw = cap.read()
print("raw read:", ok, "shape", getattr(raw, "shape", None), "dtype", getattr(raw, "dtype", None))
if ok:
    b = raw.tobytes() if raw.ndim == 1 else raw.reshape(-1).tobytes()
    print("first bytes:", b[:4].hex(), "size", len(b))

# 2) raw capture FPS over 150 frames
n = 0
t0 = time.perf_counter()
for _ in range(150):
    ok, raw = cap.read()
    if not ok:
        break
    n += 1
dt = time.perf_counter() - t0
print(f"raw capture 1080p: {n} frames in {dt:.2f}s = {n/dt:.1f} fps")
cap.release()

# 3) keep last raw frame, benchmark imdecode modes
if ok:
    last = raw.copy()
    print("compressed frame bytes:", last.size)
    for flag, name in [(cv2.IMREAD_COLOR, "COLOR"),
                       (cv2.IMREAD_REDUCED_COLOR_2, "REDUCED_2"),
                       (cv2.IMREAD_REDUCED_COLOR_4, "REDUCED_4"),
                       (cv2.IMREAD_REDUCED_COLOR_8, "REDUCED_8")]:
        # warmup
        for _ in range(3):
            img = cv2.imdecode(last, flag)
        k = 30
        t0 = time.perf_counter()
        for _ in range(k):
            img = cv2.imdecode(last, flag)
        dt = time.perf_counter() - t0
        print(f"imdecode {name:10s}: {dt/k*1000:.2f} ms/frame -> {k/dt:.1f} fps, shape={img.shape}")

# 4) end-to-end: raw + reduced decode
for red in [2, 4]:
    cap = open_cam(1920, 1080, 30, convert_rgb=False)
    flag = cv2.IMREAD_REDUCED_COLOR_2 if red == 2 else cv2.IMREAD_REDUCED_COLOR_4
    for _ in range(15):
        cap.read()
    n = 0
    t0 = time.perf_counter()
    for _ in range(100):
        ok, raw = cap.read()
        if not ok:
            break
        img = cv2.imdecode(raw, flag)
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
        n += 1
    dt = time.perf_counter() - t0
    cap.release()
    print(f"end-to-end 1080p reduced_{red}: {n/dt:.1f} fps, out={img.shape}")

print("done")
