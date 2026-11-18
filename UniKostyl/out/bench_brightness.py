#!/usr/bin/env python3
"""Measure brightness/gain effect at fixed exposure 300 (max-FPS mode)."""
import cv2, subprocess, numpy as np

DEV = "/dev/video0"

def ctl(*a):
    subprocess.run(["v4l2-ctl", "-d", DEV, *a], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def snap(exp=300, gain=100, bright=64):
    ctl("-c", "auto_exposure=1", "-c", f"exposure_time_absolute={exp}",
        "-c", f"gain={gain}", "-c", f"brightness={bright}",
        "-c", "exposure_dynamic_framerate=0", "-c", "backlight_compensation=0")
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
    cap.set(cv2.CAP_PROP_FPS, 30)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    for _ in range(20):
        ok, raw = cap.read()
    cap.release()
    img = cv2.imdecode(raw, cv2.IMREAD_REDUCED_COLOR_4)
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    return float(g.mean()), float(np.percentile(g, 95)), float((g >= 250).mean() * 100)

print("exp=300, gain x brightness: mean / p95 / saturated%")
for bright in [64, 0, -32, -64]:
    for gain in [0, 25, 50, 75, 100]:
        m, p95, sat = snap(300, gain, bright)
        print(f"  brightness={bright:<4d} gain={gain:<3d}: mean={m:5.1f} p95={p95:5.1f} sat={sat:4.1f}%", flush=True)

ctl("-c", "auto_exposure=3", "-c", "exposure_time_absolute=157", "-c", "gain=0",
    "-c", "brightness=0", "-c", "exposure_dynamic_framerate=1",
    "-c", "backlight_compensation=1")
