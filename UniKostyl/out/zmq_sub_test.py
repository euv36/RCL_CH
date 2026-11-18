import sys
import time

import zmq

host = sys.argv[1]
port = int(sys.argv[2])
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 8.0

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.setsockopt_string(zmq.SUBSCRIBE, "")
sock.setsockopt(zmq.CONFLATE, 1)
sock.setsockopt(zmq.RCVTIMEO, 200)
sock.connect(f"tcp://{host}:{port}")

t0 = time.monotonic()
got = 0
while time.monotonic() - t0 < seconds:
    try:
        msg = sock.recv_string()
        got += 1
        if got == 1:
            print("first:", msg[:120], flush=True)
    except zmq.Again:
        continue

print("received", got, "messages in", seconds, "s", flush=True)
sock.close(0)
ctx.term()
