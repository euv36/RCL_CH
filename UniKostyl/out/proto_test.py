#!/usr/bin/env python3
"""Local end-to-end test: run rpi_elp_cam.py and talk to it over ZMQ."""
import json
import subprocess
import sys
import time

import cv2
import zmq

proc = subprocess.Popen(
    [sys.executable, "rpi_elp_cam.py", "--duration", "10", "--no-serial",
     "--laptop-ip", "127.0.0.1", "--stream-fps", "20"],
    cwd="/home/twox",
)
time.sleep(4.0)

ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.setsockopt(zmq.LINGER, 0)
pub.bind("tcp://127.0.0.1:5557")

sub = ctx.socket(zmq.SUB)
sub.setsockopt_string(zmq.SUBSCRIBE, "")
sub.setsockopt(zmq.CONFLATE, 1)
sub.setsockopt(zmq.RCVTIMEO, 1000)
sub.connect("tcp://127.0.0.1:5555")

time.sleep(0.7)
for _ in range(3):
    pub.send_string(json.dumps({"stream": True}))
    pub.send_string(json.dumps({"gates": [{"id": 1, "rgb": [0, 255, 0]}, {"id": 2, "rgb": [0, 0, 255]}]}))
    time.sleep(0.3)
print("sent colors", flush=True)

count = 0
first_shape = None
t0 = time.perf_counter()
while time.perf_counter() - t0 < 5.0:
    try:
        msg = sub.recv()
    except zmq.Again:
        print("no frame (timeout)", flush=True)
        continue
    img = cv2.imdecode(__import__("numpy").frombuffer(msg, dtype="uint8"), cv2.IMREAD_COLOR)
    if img is not None:
        count += 1
        first_shape = first_shape or img.shape
        if count == 1:
            print(f"first JPEG: {len(msg)} bytes, shape={img.shape}, mean={img.mean():.1f}", flush=True)

print(f"received {count} frames in 5s, shape={first_shape}", flush=True)

proc.wait(timeout=10)
print("program exit code:", proc.returncode, flush=True)
pub.close()
sub.close()
ctx.term()
