"""Шлёт цвета 3 ворот на Pi (ZMQ PUB → tcp://*:<port>).

Pi сам делает SUB-connect к ноутбуку, поэтому здесь нужен bind на ноутбуке.
Использование: python out\\send_colors.py [секунды] [порт]
"""
import json
import sys
import time

import zmq

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
port = int(sys.argv[2]) if len(sys.argv) > 2 else 5557

ctx = zmq.Context()
sock = ctx.socket(zmq.PUB)
sock.bind(f"tcp://*:{port}")

payload = {
    "gates": [
        {"id": 1, "rgb": [255, 255, 0]},    # жёлтый
        {"id": 2, "rgb": [0, 0, 255]},      # синий
        {"id": 3, "rgb": [255, 128, 0]},    # оранжевый
    ]
}
msg = json.dumps(payload)
print(f"PUB tcp://*:{port} {msg} ({seconds:.0f} c)", flush=True)

t0 = time.monotonic()
while time.monotonic() - t0 < seconds:
    sock.send_string(msg)
    time.sleep(0.2)

sock.close(0)
ctx.term()
print("colors sent", flush=True)
