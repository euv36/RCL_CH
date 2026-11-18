#!/usr/bin/env python3
"""Check control persistence across open() and FPS after setting controls live."""
import cv2, subprocess, time

DEV = "/dev/video0"

def ctl(*a):
    r = subprocess.run(["v4l2-ctl", "-d", DEV, *a], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()

def get():
    print("GET:", ctl("--get-ctrl=auto_exposure,exposure_time_absolute,gain,brightness,exposure_dynamic_framerate"))

def fps(frames=100):
    for _ in range(10):
        cap.read()
    n = 0; t0 = time.perf_counter()
    for _ in range(frames):
        ok, raw = cap.read()
        if not ok: break
        n += 1
    dt = time.perf_counter() - t0
    print(f"FPS: {n/dt:.1f}", flush=True)

print("--- set before open ---")
print(ctl("-c", "auto_exposure=1", "-c", "exposure_time_absolute=300",
          "-c", "gain=100", "-c", "brightness=64",
          "-c", "exposure_dynamic_framerate=0"))

cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
cap.set(cv2.CAP_PROP_FPS, 30)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
get()
fps()

print("--- set live exp=300 again ---")
print(ctl("-c", "auto_exposure=1", "-c", "exposure_time_absolute=300",
          "-c", "gain=100", "-c", "brightness=64"))
get()
fps()

print("--- set live exp=150 ---")
print(ctl("-c", "exposure_time_absolute=150"))
get()
fps()

print("--- set live exp=300 ---")
print(ctl("-c", "exposure_time_absolute=300"))
get()
fps()

cap.release()
