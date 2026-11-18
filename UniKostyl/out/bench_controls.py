#!/usr/bin/env python3
"""Check whether 'exposure_dynamic_framerate' is what kills 1080p FPS."""
import cv2, time, subprocess

DEV = "/dev/video0"

def ctl(*args):
    subprocess.run(["v4l2-ctl", "-d", DEV, *args], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def measure(w, h, fps=30, frames=120, label=""):
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    for _ in range(20):
        cap.read()
    n = 0
    t0 = time.perf_counter()
    for _ in range(frames):
        ok, raw = cap.read()
        if not ok:
            break
        n += 1
    dt = time.perf_counter() - t0
    cap.release()
    print(f"{label:52s} {n/dt:5.1f} fps  ({n} frames / {dt:.2f}s)", flush=True)

print("default controls:", flush=True)
measure(1920, 1080, 30, label="1080p30 default")
measure(1280, 720, 60, label="720p60 default")

ctl("-c", "exposure_dynamic_framerate=0")
measure(1920, 1080, 30, label="1080p30 dynamic_framerate=0")
measure(1280, 720, 60, label="720p60 dynamic_framerate=0")

ctl("-c", "auto_exposure=1", "-c", "exposure_time_absolute=300", "-c", "gain=60")
measure(1920, 1080, 30, label="1080p30 manual exp=300 gain=60")
measure(1280, 720, 60, label="720p60 manual exp=300 gain=60")

ctl("-c", "auto_exposure=3", "-c", "exposure_time_absolute=157", "-c", "gain=0",
    "-c", "exposure_dynamic_framerate=1")
print("controls restored", flush=True)
