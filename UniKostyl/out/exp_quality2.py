#!/usr/bin/env python3
"""Second sweep: gain/brightness/gamma to keep real colors at 26 fps."""
import subprocess
import time

import cv2
import numpy as np

DEV = "/dev/video0"


def v4l2(**kw):
    args = ["v4l2-ctl", "-d", DEV]
    for k, v in kw.items():
        args += ["-c", f"{k}={int(v)}"]
    subprocess.run(args, capture_output=True, text=True)


def shot(tag):
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
        ok, d = cap.read()
        if ok:
            last = d
    full = cv2.imdecode(last, cv2.IMREAD_COLOR)
    small = cv2.resize(cv2.imdecode(last, cv2.IMREAD_REDUCED_COLOR_2),
                       (640, 360), interpolation=cv2.INTER_AREA)
    cv2.imwrite(f"/tmp/q_{tag}.jpg", small, [int(cv2.IMWRITE_JPEG_QUALITY), 92])
    n = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < 2.0:
        if cap.read()[0]:
            n += 1
    dt = time.perf_counter() - t0
    cap.release()
    hsv = cv2.cvtColor(full, cv2.COLOR_BGR2HSV)
    v, s = hsv[:, :, 2], hsv[:, :, 1]
    return n / dt, float(v.mean()), float(s.mean()), float((v >= 250).mean() * 100), float((v <= 5).mean() * 100)


configs = [
    ("g60_b0_gm100", 60, 0, 100),
    ("g80_b0_gm100", 80, 0, 100),
    ("g100_b0_gm100", 100, 0, 100),
    ("g60_b20_gm100", 60, 20, 100),
    ("g80_b20_gm100", 80, 20, 100),
    ("g60_b0_gm200", 60, 0, 200),
    ("g80_b0_gm200", 80, 0, 200),
    ("g100_b64_gm100", 100, 64, 100),
]
for tag, gain, bright, gamma in configs:
    v4l2(auto_exposure=1, exposure_time_absolute=300, gain=gain,
         brightness=bright, gamma=gamma, contrast=40, saturation=60,
         exposure_dynamic_framerate=0, backlight_compensation=0)
    time.sleep(1.0)
    r = shot(tag)
    if r is None:
        print(tag, "no frame", flush=True)
        continue
    print(f"{tag:15s} gain={gain:3d} b={bright:3d} gamma={gamma:3d} | "
          f"fps={r[0]:5.1f} luma={r[1]:5.1f} sat={r[2]:5.1f} "
          f"over={r[3]:4.1f}% black={r[4]:5.1f}%", flush=True)

v4l2(auto_exposure=3, gain=0, brightness=0, gamma=100, contrast=32,
     saturation=60, exposure_dynamic_framerate=1, backlight_compensation=1)
print("done", flush=True)
