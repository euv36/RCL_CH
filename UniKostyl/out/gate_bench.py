#!/usr/bin/env python3
"""Measure end-to-end FPS with gates actually enabled (worst-case mask coverage)."""
import json
import re
import subprocess
import sys
import time

import zmq

COLORS = [(0, 255, 0), (0, 0, 255), (255, 0, 0),
          (0, 255, 255), (255, 255, 0), (255, 0, 255)]


def run(gates, track, tol, label, dur=13):
    cmd = [sys.executable, "rpi_elp_cam.py", "--duration", str(dur),
           "--no-serial", "--laptop-ip", "127.0.0.1", "--gates", str(gates),
           "--track-size", track, "--tolerance", tol]
    proc = subprocess.Popen(cmd, cwd="/home/twox", stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    time.sleep(4.5)
    ctx = zmq.Context()
    pub = ctx.socket(zmq.PUB)
    pub.setsockopt(zmq.LINGER, 0)
    pub.bind("tcp://127.0.0.1:5557")
    msg = json.dumps({"gates": [{"id": i + 1, "rgb": list(COLORS[i % len(COLORS)])}
                                for i in range(gates)]})
    for _ in range(12):
        pub.send_string(msg)
        time.sleep(0.25)
    out = proc.communicate(timeout=dur + 30)[0]
    pub.close()
    ctx.term()

    fps = re.findall(r"FPS: обработка ([\d.]+), захват ([\d.]+), видео ([\d.]+)", out)
    err = [l for l in out.splitlines() if "Traceback" in l or "ERROR" in l]
    if fps:
        proc_fps = sum(float(a) for a, _, _ in fps) / len(fps)
        cap_fps = sum(float(b) for _, b, _ in fps) / len(fps)
        vid_fps = sum(float(c) for _, _, c in fps) / len(fps)
        print(f"{label:34s} gates={gates} track={track} -> "
              f"обработка {proc_fps:5.1f}  захват {cap_fps:5.1f}  видео {vid_fps:5.1f}",
              flush=True)
    else:
        print(f"{label}: no FPS lines; {err[:3]}", flush=True)


run(2, "640x360", "40,100,100", "реальный допуск")
run(2, "640x360", "90,255,255", "худший случай (широкий)")
run(4, "640x360", "90,255,255", "4 ворот, худший случай")
run(6, "640x360", "90,255,255", "6 ворот, худший случай")
run(2, "960x540", "90,255,255", "2 ворот, 960x540")
print("done", flush=True)
