#!/usr/bin/env python3
"""Search for the highest FPS that still preserves full FOV, and FOV of every mode."""
import subprocess
import sys
import time

import cv2
import numpy as np


def v4l2(**kw):
    args = ["v4l2-ctl", "-d", "/dev/video0"]
    for k, v in kw.items():
        args += ["-c", f"{k}={int(v)}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        print("v4l2 fail:", (r.stdout + r.stderr).strip()[:120], flush=True)
    return r.returncode == 0


def open_cam(w, h, fps, convert_rgb0=True):
    cap = cv2.VideoCapture("/dev/video0", cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    if convert_rgb0:
        cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    ok, first = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError(f"no frame {w}x{h}@{fps}")
    return cap, first


def measure_fps(cap, seconds=3.0):
    n = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < seconds:
        ok, _ = cap.read()
        if ok:
            n += 1
    dt = time.perf_counter() - t0
    return n / dt


print("=== FOV per mode (same scene, 1080p reference) ===", flush=True)
v4l2(auto_exposure=1, exposure_time_absolute=300, gain=100, brightness=64,
     exposure_dynamic_framerate=0)

frames = {}
for w, h, fps in [(1920, 1080, 30), (1280, 720, 60), (1024, 768, 30),
                  (1280, 1024, 30), (800, 600, 60), (640, 480, 120),
                  (320, 240, 120)]:
    try:
        cap, first = open_cam(w, h, fps)
        for _ in range(20):
            ok, data = cap.read()
            if ok:
                first = data
        img = cv2.imdecode(first, cv2.IMREAD_COLOR) if getattr(first, "ndim", 0) != 3 else first
        cap.release()
        if img is None:
            print(f"{w}x{h}: decode fail", flush=True)
            continue
        frames[(w, h)] = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        print(f"{w}x{h}: captured {img.shape[1]}x{img.shape[0]}", flush=True)
    except Exception as e:
        print(f"{w}x{h}: {e}", flush=True)

ref = frames[(1920, 1080)]
orb = cv2.ORB_create(nfeatures=8000, fastThreshold=8)
kr, dr = orb.detectAndCompute(ref, None)
matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
rh, rw = ref.shape
for key, img in frames.items():
    if key == (1920, 1080):
        continue
    h, w = img.shape
    km, dm = orb.detectAndCompute(img, None)
    if dm is None or len(km) < 20:
        print(f"{w}x{h}: too few features", flush=True)
        continue
    m = sorted(matcher.match(dm, dr), key=lambda x: x.distance)[:1500]
    src = np.float32([km[x.queryIdx].pt for x in m]).reshape(-1, 1, 2)
    dst = np.float32([kr[x.trainIdx].pt for x in m]).reshape(-1, 1, 2)
    H, mask = cv2.findHomography(src, dst, cv2.RANSAC, 3.0)
    if H is None:
        print(f"{w}x{h}: homography fail", flush=True)
        continue
    c = cv2.perspectiveTransform(
        np.float32([[0, 0], [w, 0], [w, h], [0, h]]).reshape(-1, 1, 2), H).reshape(-1, 2)
    x0, y0 = c.min(axis=0)
    x1, y1 = c.max(axis=0)
    print(f"{w}x{h}: inliers={int(mask.sum())} covers {(x1-x0)/rw*100:5.1f}% x "
          f"{(y1-y0)/rh*100:5.1f}% of 1080p", flush=True)

print("\n=== Max FPS at 1920x1080 MJPG (full FOV) ===", flush=True)
trials = [
    ("exposure=300 gain=100 dynfps=0", dict(exposure_time_absolute=300, gain=100, exposure_dynamic_framerate=0)),
    ("exposure=200 gain=100 dynfps=0", dict(exposure_time_absolute=200, gain=100, exposure_dynamic_framerate=0)),
    ("exposure=120 gain=200 dynfps=0", dict(exposure_time_absolute=120, gain=200, exposure_dynamic_framerate=0)),
    ("exposure=60  gain=255 dynfps=0", dict(exposure_time_absolute=60, gain=255, exposure_dynamic_framerate=0)),
    ("exposure=30  gain=255 dynfps=0", dict(exposure_time_absolute=30, gain=255, exposure_dynamic_framerate=0)),
]
for name, ctrls in trials:
    v4l2(auto_exposure=1, brightness=64, **ctrls)
    for req in (30, 60):
        try:
            cap, _ = open_cam(1920, 1080, req)
            real = cap.get(cv2.CAP_PROP_FPS)
            for _ in range(10):
                cap.read()
            fps = measure_fps(cap, 3.0)
            cap.release()
            print(f"{name:32s} request={req:2d} real={real:5.1f} -> {fps:5.1f} fps", flush=True)
        except Exception as e:
            print(f"{name} request={req}: {e}", flush=True)

print("\n=== Lower-res full-bandwidth check ===", flush=True)
v4l2(auto_exposure=1, exposure_time_absolute=300, gain=100, brightness=64,
     exposure_dynamic_framerate=0)
for w, h, fps in [(1280, 720, 60), (640, 480, 120), (320, 240, 120)]:
    cap, _ = open_cam(w, h, fps)
    for _ in range(10):
        cap.read()
    f = measure_fps(cap, 3.0)
    cap.release()
    print(f"{w}x{h} request={fps}: {f:5.1f} fps", flush=True)

v4l2(auto_exposure=3, gain=0, brightness=0, exposure_dynamic_framerate=1,
     backlight_compensation=1)
print("done", flush=True)
