#!/usr/bin/env python3
"""Temporary Pi service smoke test: read multipart [JPEG, JSON] from ZMQ.

This script performs a real network connection only when a human runs it.
It is not part of CTest; C++ loopback coverage lives in pi_zmq_loopback_test.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

try:
    import zmq
except ImportError as exc:  # pragma: no cover - dependency gate for human runs
    print(json.dumps({"ok": False, "error": f"pyzmq missing: {exc}"}))
    sys.exit(2)


SOI = b"\xff\xd8"
EOI = b"\xff\xd9"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Read one native Pi video frame and status.")
    parser.add_argument("--host", default="10.132.122.51", help="Pi address")
    parser.add_argument("--video-port", type=int, default=5555)
    parser.add_argument("--cam-settings-port", type=int, default=5559)
    parser.add_argument("--duration", type=float, default=5.0, help="maximum wait in seconds")
    parser.add_argument("--probe-cam-set", action="store_true",
                        help="opt-in: send integer gamma=100; no ack socket is expected")
    parser.add_argument("--output", type=Path, help="optional JSON summary output path")
    return parser.parse_args()


def probe_cam_set(host: str, port: int) -> None:
    context = zmq.Context.instance()
    sender = context.socket(zmq.PUB)
    sender.setsockopt(zmq.LINGER, 0)
    sender.connect(f"tcp://{host}:{port}")
    try:
        time.sleep(0.2)
        sender.send_json({"cmd": "set", "controls": {"gamma": 100}})
    finally:
        sender.close()


def write_summary(path: Path, summary: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(summary, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        os.replace(temp_name, path)
    except BaseException:
        if os.path.exists(temp_name):
            os.unlink(temp_name)
        raise


def main() -> int:
    args = parse_args()
    if args.duration <= 0:
        print(json.dumps({"ok": False, "error": "--duration must be positive"}))
        return 2

    if args.probe_cam_set:
        probe_cam_set(args.host, args.cam_settings_port)

    context = zmq.Context.instance()
    subscriber = context.socket(zmq.SUB)
    subscriber.setsockopt(zmq.RCVTIMEO, 250)
    subscriber.setsockopt(zmq.LINGER, 0)
    subscriber.setsockopt_string(zmq.SUBSCRIBE, "")
    subscriber.connect(f"tcp://{args.host}:{args.video_port}")

    summary: dict = {
        "host": args.host,
        "video_port": args.video_port,
        "duration_s": args.duration,
        "messages": 0,
        "valid_jpeg": 0,
        "valid_json": 0,
        "first_status": None,
        "cam_set_probe": bool(args.probe_cam_set),
        "ok": False,
    }
    deadline = time.monotonic() + args.duration
    try:
        while time.monotonic() < deadline:
            try:
                frames = subscriber.recv_multipart()
            except zmq.Again:
                continue
            if len(frames) != 2:
                continue
            summary["messages"] += 1
            jpeg, payload = frames
            if jpeg.startswith(SOI) and jpeg.endswith(EOI):
                summary["valid_jpeg"] += 1
            try:
                status = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if not isinstance(status, dict) or "gates" not in status:
                continue
            summary["valid_json"] += 1
            if summary["first_status"] is None:
                summary["first_status"] = status
            if summary["valid_jpeg"] and summary["valid_json"]:
                summary["ok"] = True
                break
    finally:
        subscriber.close()

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if args.output is not None:
        write_summary(args.output, summary)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
