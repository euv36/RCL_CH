#!/usr/bin/env python3
"""Разложение frame_from_raw по частям на свежих кадрах + влияние cv2.setNumThreads."""
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, "/home/twox")
import rpi_elp_cam as R

R.apply_camera_controls("/dev/video0", 300, 0, 0)
cam = R.RawCamera("/dev/video0", 1920, 1080, 30)

flags = {"REDUCED_2": cv2.IMREAD_REDUCED_COLOR_2,
         "REDUCED_4": cv2.IMREAD_REDUCED_COLOR_4}

for threads in (1, 4):
    cv2.setNumThreads(threads)
    print(f"=== cv2 threads={cv2.getNumThreads()} ===", flush=True)
    for name, flag in flags.items():
        t_dec = t_res = t_all = 0.0
        n = 0
        for _ in range(20):
            ok, data = cam.read()
            if not ok or data is None:
                continue
            t0 = time.perf_counter()
            img = cv2.imdecode(data, flag)
            t1 = time.perf_counter()
            if img is None:
                continue
            cv2.resize(img, (640, 360), interpolation=cv2.INTER_AREA)
            t2 = time.perf_counter()
            R.frame_from_raw(data, 1920, 640, 360, flag)
            t3 = time.perf_counter()
            t_dec += t1 - t0
            t_res += t2 - t1
            t_all += t3 - t2
            n += 1
        print(f"  {name}: imdecode {t_dec/n*1000:5.1f}  resize {t_res/n*1000:5.1f}  "
              f"frame_from_raw {t_all/n*1000:5.1f} мс", flush=True)

cam.release()
