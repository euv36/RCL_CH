#!/usr/bin/env python3
"""FPS vs manual exposure; also report mean brightness."""
import cv2, time, subprocess, numpy as np

DEV = "/dev/video0"

def ctl(*a):
    subprocess.run(["v4l2-ctl", "-d", DEV, *a], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def measure(w, h, fps, exp, gain, frames=100, save=None):
    ctl("-c", "auto_exposure=1", "-c", f"exposure_time_absolute={exp}",
        "-c", f"gain={gain}", "-c", "exposure_dynamic_framerate=0")
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    last = None
    for _ in range(20):
        ok, last = cap.read()
    n = 0
    t0 = time.perf_counter()
    for _ in range(frames):
        ok, raw = cap.read()
        if not ok:
            break
        last = raw
        n += 1
    dt = time.perf_counter() - t0
    cap.release()
    img = cv2.imdecode(last, cv2.IMREAD_REDUCED_COLOR_4) if last is not None else None
    mean = float(img.mean()) if img is not None else -1
    print(f"{w}x{h}@{fps:<3d} exp={exp:<4d} gain={gain:<3d} -> {n/dt:5.1f} fps  mean={mean:5.1f}", flush=True)
    if save:
        cv2.imwrite(save, img)

for exp, gain in [(50, 100), (100, 100), (150, 100), (200, 100), (250, 100), (300, 100)]:
    measure(1920, 1080, 30, exp, gain, save=f"/tmp/exp_{exp}_{gain}.jpg")
print("---")
for exp, gain in [(50, 100), (100, 100), (150, 100), (200, 100)]:
    measure(1280, 720, 60, exp, gain)

ctl("-c", "auto_exposure=3", "-c", "exposure_time_absolute=157", "-c", "gain=0",
    "-c", "exposure_dynamic_framerate=1")
