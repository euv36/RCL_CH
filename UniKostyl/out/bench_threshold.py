#!/usr/bin/env python3
"""Find the exposure/FPS relationship at 1080p and 720p."""
import cv2, time, subprocess

DEV = "/dev/video0"

def ctl(*a):
    subprocess.run(["v4l2-ctl", "-d", DEV, *a], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def measure(w, h, fps, exp, gain=100, frames=80):
    ctl("-c", "auto_exposure=1", "-c", f"exposure_time_absolute={exp}",
        "-c", f"gain={gain}", "-c", "exposure_dynamic_framerate=0")
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    for _ in range(15):
        cap.read()
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
    img = cv2.imdecode(last, cv2.IMREAD_REDUCED_COLOR_4)
    print(f"1080p exp={exp:<4d} gain={gain:<3d} -> {n/dt:5.1f} fps  mean={img.mean():5.1f}", flush=True)

for exp in [250, 260, 270, 280, 290, 295, 300, 310, 320, 350, 400, 500]:
    measure(1920, 1080, 30, exp)
print("--- gain sweep at exp=300 ---")
for gain in [0, 20, 40, 60, 80, 100]:
    measure(1920, 1080, 30, 300, gain)
print("--- 720p ---")
for exp in [100, 150, 200, 250, 300]:
    measure(1280, 720, 60, exp)
ctl("-c", "auto_exposure=3", "-c", "exposure_time_absolute=157", "-c", "gain=0",
    "-c", "exposure_dynamic_framerate=1")
