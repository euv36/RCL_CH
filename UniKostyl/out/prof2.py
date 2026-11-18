#!/usr/bin/env python3
"""Проверка: замедляет ли CaptureThread декодирование в главном потоке."""
import sys
import time

import cv2

sys.path.insert(0, "/home/twox")
import rpi_elp_cam as R

R.apply_camera_controls("/dev/video0", 300, 0, 0)
cam = R.RawCamera("/dev/video0", 1920, 1080, 30)
flag = cv2.IMREAD_REDUCED_COLOR_2


def measure(getter, n, label):
    total = 0.0
    k = 0
    fails = 0
    while k < n and fails < 200:
        data = getter()
        if data is None:
            fails += 1
            time.sleep(0.005)
            continue
        t0 = time.perf_counter()
        R.frame_from_raw(data, 1920, 640, 360, flag)
        total += time.perf_counter() - t0
        k += 1
    print(f"{label}: {total/max(1,k)*1000:.1f} мс/кадр ({k} кадров)", flush=True)


def seq_get():
    ok, data = cam.read()
    return data if ok else None


measure(seq_get, 20, "A) без потока (последовательно)")

# теперь как в программе: поток всё время вычитывает камеру
cap = R.CaptureThread(cam)
cap.start()
time.sleep(0.5)
state = {"last": -1}


def threaded_get():
    data, seq = cap.get_latest(state["last"])
    if data is None:
        return None
    state["last"] = seq
    return data


measure(threaded_get, 20, "B) с CaptureThread")
print("fps захвата в потоке:", round(cap.fps(), 1), flush=True)
cap.stop()
cap.join(timeout=1)
cam.release()
