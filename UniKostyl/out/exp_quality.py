#!/usr/bin/env python3
"""Compare exposure/gain/brightness: clipping, saturation, FPS. Saves samples."""
import os
import subprocess
import time

import cv2
import numpy as np

DEV = "/dev/video0"


def v4l2(**kw):
    args = ["v4l2-ctl", "-d", DEV]
    for k, v in kw.items():
        args += ["-c", f"{k}={int(v)}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        print("  fail:", (r.stdout + r.stderr).strip()[:100], flush=True)


def grab(tag):
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
    cap.set(cv2.CAP_PROP_FPS, 30)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    if not cap.read()[0]:
        cap.release()
        return None
    for _ in range(12):
        ok, data = cap.read()
        if ok:
            last = data
    img = cv2.imdecode(last, cv2.IMREAD_COLOR)
    small = cv2.resize(cv2.imdecode(last, cv2.IMREAD_REDUCED_COLOR_2),
                       (640, 360), interpolation=cv2.INTER_AREA)
    cv2.imwrite(f"/tmp/exp_{tag}.jpg", small, [int(cv2.IMWRITE_JPEG_QUALITY), 92])

    n = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < 2.0:
        if cap.read()[0]:
            n += 1
    dt = time.perf_counter() - t0
    cap.release()

    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    v = hsv[:, :, 2]
    s = hsv[:, :, 1]
    over = float((v >= 250).mean() * 100)
    under = float((v <= 5).mean() * 100)
    return dict(fps=n / dt, luma=float(v.mean()), sat=float(s.mean()),
                over=over, under=under)


print(subprocess.run(["v4l2-ctl", "-d", DEV, "-l"], capture_output=True,
                     text=True).stdout, flush=True)

configs = [
    ("cur_300_100_64", 300, 100, 64),
    ("e300_g50_b0", 300, 50, 0),
    ("e300_g0_b0", 300, 0, 0),
    ("e290_g0_b0", 290, 0, 0),
    ("e270_g0_b0", 270, 0, 0),
    ("e260_g0_b0", 260, 0, 0),
    ("e250_g0_b0", 250, 0, 0),
    ("e200_g0_b0", 200, 0, 0),
]
for tag, expo, gain, bright in configs:
    v4l2(auto_exposure=1, exposure_time_absolute=expo, gain=gain,
         brightness=bright, exposure_dynamic_framerate=0,
         backlight_compensation=0)
    time.sleep(1.2)
    r = grab(tag)
    if r is None:
        print(f"{tag}: no frame", flush=True)
        continue
    print(f"{tag:16s} expo={expo:3d} gain={gain:3d} bright={bright:3d} | "
          f"fps={r['fps']:5.1f} luma={r['luma']:5.1f} sat={r['sat']:5.1f} "
          f"over250={r['over']:5.1f}% black={r['under']:5.1f}%", flush=True)

v4l2(auto_exposure=3, gain=0, brightness=0, exposure_dynamic_framerate=1,
     backlight_compensation=1)
print("done", flush=True)
