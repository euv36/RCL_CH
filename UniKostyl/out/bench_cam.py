#!/usr/bin/env python3
"""Benchmark ELP UVC camera: FOV snapshots + real FPS at different modes."""
import cv2, time, os, sys

DEV = "/dev/video0"
OUT = "/tmp/cam_bench"
os.makedirs(OUT, exist_ok=True)

MODES = [
    (1920, 1080),
    (1280, 720),
    (1024, 768),
    (800, 600),
    (640, 480),
    (1280, 1024),
]

def fourcc_str(v):
    return "".join(chr((v >> (8 * i)) & 0xFF) for i in range(4))

def capture(w, h, label, frames_warm=30, frames_measure=60):
    cap = cv2.VideoCapture(DEV, cv2.CAP_V4L2)
    if not cap.isOpened():
        print(f"{label}: FAILED to open", flush=True)
        return
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_FPS, 60)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    fw = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    fh = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    ff = int(cap.get(cv2.CAP_PROP_FOURCC))
    fps_prop = cap.get(cv2.CAP_PROP_FPS)

    # warmup / let AE settle
    last = None
    for _ in range(frames_warm):
        ok, last = cap.read()
        if not ok:
            break
    if last is None:
        print(f"{label}: no frames", flush=True)
        cap.release()
        return
    cv2.imwrite(os.path.join(OUT, f"{label}.jpg"), last,
                [int(cv2.IMWRITE_JPEG_QUALITY), 90])

    # measure
    n = 0
    t0 = time.perf_counter()
    for _ in range(frames_measure):
        ok, fr = cap.read()
        if not ok:
            break
        n += 1
    dt = time.perf_counter() - t0
    cap.release()
    print(f"{label}: requested={w}x{h} actual={fw}x{fh} fourcc={fourcc_str(ff)} "
          f"fps_prop={fps_prop:.1f} measured={n/dt:.1f} fps", flush=True)

for (w, h) in MODES:
    capture(w, h, f"{w}x{h}")

print("done", flush=True)
