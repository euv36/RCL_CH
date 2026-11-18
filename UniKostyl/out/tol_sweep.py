#!/usr/bin/env python3
"""Подбор допуска HSV для 3 реальных цветов: замер FPS и площади масок.

Запускается НА Pi: python3 /tmp/tol_sweep.py
"""
import json
import re
import subprocess
import sys
import time

import zmq

COLORS = [(255, 255, 0), (0, 0, 255), (255, 128, 0)]  # жёлтый, синий, оранжевый
TOLS = ["40,100,100", "30,120,80", "25,140,70", "20,150,60", "15,160,50"]


def run(tol, dur=11):
    cmd = [sys.executable, "rpi_elp_cam.py", "--duration", str(dur),
           "--no-serial", "--laptop-ip", "127.0.0.1", "--gates", "3",
           "--track-size", "640x360", "--tolerance", tol]
    proc = subprocess.Popen(cmd, cwd="/home/twox", stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    time.sleep(4.0)
    ctx = zmq.Context()
    pub = ctx.socket(zmq.PUB)
    pub.setsockopt(zmq.LINGER, 0)
    pub.bind("tcp://127.0.0.1:5557")
    msg = json.dumps({"gates": [{"id": i + 1, "rgb": list(COLORS[i])}
                                for i in range(3)]})
    t_end = time.monotonic() + dur
    while time.monotonic() < t_end:
        pub.send_string(msg)
        time.sleep(0.25)
    out = proc.communicate(timeout=dur + 30)[0]
    pub.close()
    ctx.term()

    fps = re.findall(r"FPS: обработка ([\d.]+), захват ([\d.]+), видео ([\d.]+)", out)
    if not fps:
        print(f"tol={tol:12s} нет строк FPS", flush=True)
        return
    p = sum(float(a) for a, _, _ in fps) / len(fps)
    c = sum(float(b) for _, b, _ in fps) / len(fps)
    v = sum(float(x) for _, _, x in fps) / len(fps)
    print(f"tol={tol:12s} -> обработка {p:5.1f}  захват {c:5.1f}  видео {v:5.1f}",
          flush=True)


for tol in TOLS:
    run(tol)
print("done", flush=True)
