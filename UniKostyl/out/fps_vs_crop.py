#!/usr/bin/env python3
"""Does a cropped mode give more FPS than 1080p at the same exposure?"""
import subprocess
import time

import cv2


def v4l2(**kw):
    args = ["v4l2-ctl", "-d", "/dev/video0"]
    for k, v in kw.items():
        args += ["-c", f"{k}={int(v)}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        print("  v4l2:", (r.stdout + r.stderr).strip()[:90], flush=True)


def bench(w, h, req, seconds=3.0):
    cap = cv2.VideoCapture("/dev/video0", cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, req)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    ok, _ = cap.read()
    if not ok:
        cap.release()
        return None
    for _ in range(8):
        cap.read()
    n = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < seconds:
        if cap.read()[0]:
            n += 1
    dt = time.perf_counter() - t0
    cap.release()
    return n / dt


print("USB topology:", flush=True)
r = subprocess.run("lsusb -t", shell=True, capture_output=True, text=True)
print(r.stdout or r.stderr, flush=True)

print("=== cropped modes at short exposure (can they beat 26?) ===", flush=True)
for expo in (300, 100, 30):
    v4l2(auto_exposure=1, exposure_time_absolute=expo, gain=100, brightness=64,
         exposure_dynamic_framerate=0)
    print(f"exposure={expo}", flush=True)
    for w, h, req in [(1920, 1080, 30), (1280, 720, 60), (640, 480, 120)]:
        f = bench(w, h, req)
        print(f"  {w}x{h} req={req:3d} -> {f and round(f,1)} fps", flush=True)

print("=== auto exposure (camera picks its own) ===", flush=True)
v4l2(auto_exposure=3, gain=0, brightness=0, exposure_dynamic_framerate=1,
     backlight_compensation=1)
for w, h, req in [(1920, 1080, 30), (1280, 720, 60), (640, 480, 120)]:
    f = bench(w, h, req)
    print(f"  {w}x{h} req={req:3d} -> {f and round(f,1)} fps", flush=True)
print("done", flush=True)
