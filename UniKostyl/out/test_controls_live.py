#!/usr/bin/env python3
"""Verify UVC controls can be changed while OpenCV is streaming."""
import cv2, subprocess, numpy as np, time

DEV = "/dev/video0"

def setc(**kw):
    args = []
    for k, v in kw.items():
        args += ["-c", f"{k}={v}"]
    r = subprocess.run(["v4l2-ctl", "-d", DEV, *args], capture_output=True, text=True)
    if r.returncode != 0:
        print("SET FAILED:", r.stdout.strip(), r.stderr.strip())

cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
cap.set(cv2.CAP_PROP_FPS, 30)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
for _ in range(20):
    cap.read()

def mean_frame(tag):
    for _ in range(15):
        ok, raw = cap.read()
    img = cv2.imdecode(raw, cv2.IMREAD_REDUCED_COLOR_4)
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    print(tag, "mean", round(float(g.mean()), 1), flush=True)

setc(auto_exposure=1, exposure_time_absolute=300, gain=100,
     brightness=64, exposure_dynamic_framerate=0)
mean_frame("brightness=64")
setc(brightness=0)
mean_frame("brightness=0")
setc(brightness=64)
mean_frame("brightness=64 again")
setc(brightness=0, gain=0)
mean_frame("brightness=0 gain=0")
setc(brightness=64, gain=100)
mean_frame("restored")
cap.release()
