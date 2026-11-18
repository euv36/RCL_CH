#!/usr/bin/env python3
"""Realistic end-to-end pipeline benchmark with full-FOV 1080p capture."""
import cv2, time, subprocess, numpy as np

DEV = "/dev/video0"
subprocess.run(["v4l2-ctl", "-d", DEV,
                "-c", "auto_exposure=1", "-c", "exposure_time_absolute=295",
                "-c", "gain=100", "-c", "exposure_dynamic_framerate=0"],
               check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
cap.set(cv2.CAP_PROP_FPS, 30)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)

print("actual:", cap.get(cv2.CAP_PROP_FRAME_WIDTH), cap.get(cv2.CAP_PROP_FRAME_HEIGHT),
      cap.get(cv2.CAP_PROP_FPS))

for _ in range(20):
    cap.read()

# variants: output size, interpolation
for out_w, out_h in [(960, 540), (640, 360)]:
    for interp, iname in [(cv2.INTER_AREA, "AREA"), (cv2.INTER_LINEAR, "LINEAR")]:
        n = 0
        t0 = time.perf_counter()
        t_dec = t_res = t_cvt = t_enc = 0.0
        bytes_total = 0
        for _ in range(150):
            ok, raw = cap.read()
            if not ok:
                break
            a = time.perf_counter()
            img = cv2.imdecode(raw, cv2.IMREAD_REDUCED_COLOR_2)  # 960x540
            b = time.perf_counter()
            small = cv2.resize(img, (out_w, out_h), interpolation=interp)
            c = time.perf_counter()
            hsv = cv2.cvtColor(small, cv2.COLOR_BGR2HSV)
            d = time.perf_counter()
            okj, jpg = cv2.imencode(".jpg", small, [int(cv2.IMWRITE_JPEG_QUALITY), 55])
            e = time.perf_counter()
            t_dec += b - a; t_res += c - b; t_cvt += d - c; t_enc += e - d
            bytes_total += len(jpg) if okj else 0
            n += 1
        dt = time.perf_counter() - t0
        print(f"out={out_w}x{out_h} {iname:6s}: {n/dt:5.1f} fps | "
              f"decode={t_dec/n*1000:5.1f} resize={t_res/n*1000:5.1f} "
              f"hsv={t_cvt/n*1000:5.1f} jpeg={t_enc/n*1000:5.1f} ms | "
              f"jpeg={bytes_total/n/1024:.0f} KB", flush=True)

cap.release()
