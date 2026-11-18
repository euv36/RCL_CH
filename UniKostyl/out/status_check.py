# -*- coding: utf-8 -*-
"""out/status_check.py — camera-free verification of Idea 2 (no hardware).

Covers:
  A. parse_status_payload: valid/invalid/forward-compatible payloads.
  B. build_gate_status + GateTracker on synthetic frames (mask_check pattern).
  C. ZMQ round-trip: single JPEG vs multipart [JPEG, JSON] on one PUB socket
     (the exact backward-compatibility contract of Idea 2).
  D. unikostyl UI: panel widget with Pi status and local tracking, cost of
     main_loop_frame and panel.draw (FPS regression check), screenshots to
     out/status_panel_preview_*.png.
"""
import json
import os
import sys
import time

import numpy as np
import pygame
import zmq

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import status_panel as SP

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


# ============ A. parse_status_payload ============
print("=== A. parse_status_payload ===")

good = {
    "gates": {
        "1": {"enabled": True, "found": True, "angle": 123, "area": 456},
        "2": {"enabled": True, "found": False},
        "3": {"enabled": False, "found": False},
    },
    "fps": 25.9,
}
res = SP.parse_status_payload(json.dumps(good).encode())
check("valid bytes payload parses", res is not None)
check("gate 1 found+angle+area kept",
      res["gates"]["1"]["found"] is True and res["gates"]["1"]["angle"] == 123
      and res["gates"]["1"]["area"] == 456)
check("gate 2 lost without angle", res["gates"]["2"]["found"] is False
      and "angle" not in res["gates"]["2"])
check("gate 3 off", res["gates"]["3"]["enabled"] is False)
check("fps extracted", res["fps"] == 25.9)
check("ts stamped", isinstance(res["ts"], float))

res = SP.parse_status_payload(json.dumps(good))
check("str payload parses too", res is not None)

fwd = {"gates": {"1": {"enabled": True, "found": True, "angle": 10.5,
                       "area": 100.7, "dist_cm": 148.0, "dist_reliable": False,
                       "role": "ours"}},
       "fps": 25.9, "edge_sectors": [0, 1]}
res = SP.parse_status_payload(json.dumps(fwd).encode())
check("Idea 6/9 fields pass through",
      res["gates"]["1"].get("dist_cm") == 148.0
      and res["gates"]["1"].get("dist_reliable") is False
      and res["gates"]["1"].get("role") == "ours")
check("extra top-level keys in extra",
      res["extra"].get("edge_sectors") == [0, 1])

check("malformed JSON -> None",
      SP.parse_status_payload(b"{not json") is None)
check("non-dict JSON -> None",
      SP.parse_status_payload(b"[1,2,3]") is None)
check("dict without gates -> None",
      SP.parse_status_payload(b'{"fps": 20}') is None)
check("gates not a dict -> None",
      SP.parse_status_payload(b'{"gates": [1,2]}') is None)
check("empty bytes -> None", SP.parse_status_payload(b"") is None)
res = SP.parse_status_payload(json.dumps(
    {"gates": {"1": {"found": True, "angle": "bad"}, "junk": 5}}).encode())
check("junk gate entries skipped / junk values kept raw",
      res is not None and "junk" not in res["gates"]
      and res["gates"]["1"]["angle"] == "bad")
res = SP.parse_status_payload(
    json.dumps({"gates": {"1": {"found": "yes"}}}).encode())
check("found coerced to bool", res["gates"]["1"]["found"] is True)

# ============ B. build_gate_status + synthetic tracking ============
print("=== B. build_gate_status (synthetic frames, no camera) ===")
import rpi_elp_cam as R

H, W = 360, 640
tracker = R.GateTracker(W, H, (40, 100, 100), 8, 1.0, 3)
#_gate_status built for disabled gates first:
st = R.build_gate_status(tracker, {}, 25.9)
check("all gates disabled by default",
      all(not st["gates"][k]["enabled"] and not st["gates"][k]["found"]
          for k in ("1", "2", "3")))
check("fps in status", st["fps"] == 25.9)
try:
    json.dumps(st, separators=(",", ":"))
    check("status JSON-serializable", True)
except (TypeError, ValueError) as e:
    check("status JSON-serializable", False, str(e))

tracker.update_gate_from_rgb(0, (255, 255, 0))   # gate 1 yellow
tracker.update_gate_from_rgb(1, (0, 0, 255))     # gate 2 blue
frame = np.full((H, W, 3), 40, np.uint8)          # dark grey BGR background
frame[100:160, 200:300] = (0, 255, 255)           # yellow blob
frame[250:300, 400:470] = (255, 0, 0)             # blue blob
import cv2
cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
gate_infos = {}
for g in range(3):
    if tracker.params[g]["enabled"]:
        gate_infos[g] = tracker.process(g)
st = R.build_gate_status(tracker, gate_infos, 25.9)
check("gate 1 found with angle/area",
      st["gates"]["1"]["found"] and st["gates"]["1"]["angle"] > 0
      and st["gates"]["1"]["area"] > 0)
check("gate 2 found", st["gates"]["2"]["found"])
check("gate 3 enabled=False, not found",
      not st["gates"]["3"]["enabled"] and not st["gates"]["3"]["found"])
print(f"    status sample: {json.dumps(st, separators=(',', ':'))}")

# --- B2. Настоящий Streamer: формат на проводе (multipart и старый режим) ---
print("=== B2. Streamer thread wire format ===")
ctx2 = zmq.Context()
probe = ctx2.socket(zmq.PUB)
free_port = probe.bind_to_random_port("tcp://127.0.0.1")
probe.close()

img = np.full((16, 16, 3), 90, np.uint8)


def recv_from_streamer(streamer, want):
    subx = ctx2.socket(zmq.SUB)
    subx.setsockopt_string(zmq.SUBSCRIBE, "")
    subx.setsockopt(zmq.LINGER, 0)
    subx.connect(f"tcp://127.0.0.1:{free_port}")
    time.sleep(0.4)
    got = None
    for _ in range(60):
        try:
            got = subx.recv_multipart(flags=zmq.NOBLOCK)
            break
        except zmq.Again:
            streamer.publish(img, want)
            time.sleep(0.05)
    subx.close()
    return got


# multipart [JPEG, JSON]
streamer = R.Streamer(ctx2, free_port, 60, 55, True)
streamer.start()
st_full = R.build_gate_status(tracker, gate_infos, 25.9)
got = recv_from_streamer(streamer, st_full)
check("streamer sends multipart [JPEG, JSON]",
      got is not None and len(got) == 2,
      f"parts={len(got) if got else 0}")
parsed2 = SP.parse_status_payload(got[1]) if got and len(got) > 1 else None
check("streamer JSON part parses with fps", parsed2 is not None
      and parsed2["fps"] == 25.9 and parsed2["gates"]["1"]["found"])
streamer.stop()
streamer.join(timeout=1.5)

# старый режим (status=None) — одиночный JPEG
streamer = R.Streamer(ctx2, free_port, 60, 55, True)
streamer.start()
got = recv_from_streamer(streamer, None)
check("streamer with status=None sends single-part JPEG",
      got is not None and len(got) == 1,
      f"parts={len(got) if got else 0}")
streamer.stop()
streamer.join(timeout=1.5)
ctx2.term()

# ============ C. ZMQ round-trip: old single JPEG + new multipart ============
print("=== C. ZMQ round-trip (backward compatibility) ===")
import zmq

ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
port = pub.bind_to_random_port("tcp://127.0.0.1")
sub = ctx.socket(zmq.SUB)
sub.setsockopt_string(zmq.SUBSCRIBE, "")
sub.setsockopt(zmq.LINGER, 0)
sub.connect(f"tcp://127.0.0.1:{port}")
time.sleep(0.3)  # пусть подписка устаканится

jpeg = (np.zeros((8, 8, 3), np.uint8)).tobytes()
jpeg_img = np.full((4, 4, 3), 200, np.uint8)
ok, jpg = cv2.imencode(".jpg", jpeg_img)
jpg_bytes = jpg.tobytes()

# old protocol: single-part JPEG
pub.send(jpg_bytes)
time.sleep(0.15)
parts = sub.recv_multipart(flags=zmq.NOBLOCK)
check("old single JPEG -> recv_multipart gives 1 part", len(parts) == 1)
check("part 0 decodes as JPEG",
      cv2.imdecode(np.frombuffer(parts[0], np.uint8), cv2.IMREAD_COLOR)
      is not None)

# new protocol: multipart [JPEG, JSON]
pub.send_multipart([jpg_bytes, json.dumps(st).encode()])
time.sleep(0.15)
parts = sub.recv_multipart(flags=zmq.NOBLOCK)
check("new multipart -> 2 parts", len(parts) == 2)
parsed = SP.parse_status_payload(parts[1])
check("part 1 parses as status with gate 1 found",
      parsed is not None and parsed["gates"]["1"]["found"])

# mixed stream: old client behavior (plain recv, no multipart awareness)
sub2 = ctx.socket(zmq.SUB)
sub2.setsockopt_string(zmq.SUBSCRIBE, "")
sub2.setsockopt(zmq.LINGER, 0)
sub2.connect(f"tcp://127.0.0.1:{port}")
time.sleep(0.3)
pub.send_multipart([jpg_bytes, json.dumps(st).encode()])
pub.send_multipart([jpg_bytes, json.dumps(st).encode()])
time.sleep(0.2)
seq = []
for _ in range(4):
    try:
        seq.append(sub2.recv(flags=zmq.NOBLOCK))
    except zmq.Again:
        break
kinds = ["jpeg" if cv2.imdecode(np.frombuffer(m, np.uint8),
                                cv2.IMREAD_COLOR) is not None else "json"
         for m in seq]
check("old un-upgraded client sees alternating jpeg/json (documented "
      "degradation, not a crash)", len(seq) == 4 and kinds[0] == "jpeg"
      and kinds[1] == "json", str(kinds))

sub.close(); sub2.close(); pub.close(); ctx.term()

# ============ D. UI panel with real unikostyl ============
print("=== D. unikostyl UI panel (window flashes briefly) ===")
out_dir = os.path.dirname(os.path.abspath(__file__))
import unikostyl as U

panel = U.widgets.get("status_panel")
check("status_panel widget registered", panel is not None)
check("panel is display-only (mousedown passes through)",
      panel.process_mousedown(type("E", (), {"pos": (0, 0)})()) is False)

frame_rgb = np.zeros((360, 640, 3), np.uint8)
frame_rgb[:, :, 0] = 120  # что-то серо-цветное
frame_rgb[280:320, 40:120] = (255, 255, 0)  # жёлтый кусок для локального трекинга

# Локальный режим: слот 0 настроен на жёлтый. ВАЖНО: main_loop_frame каждый
# кадр переливает значения слайдеров в thresholds[thr_index=0], поэтому
# настраиваем именно слайдеры, а не thresholds напрямую.
for name, val in [("slider_L_low", 50), ("slider_L_high", 100),
                  ("slider_A_low", -60), ("slider_A_high", 30),
                  ("slider_B_low", 20), ("slider_B_high", 127)]:
    U.widgets[name].set_value(val)


def run_frames(n, frame):
    t0 = time.perf_counter()
    for _ in range(n):
        U.main_loop_frame(frame)
    return (time.perf_counter() - t0) / n * 1000


run_frames(10, frame_rgb)  # прогрев (кэш шрифтов, компиляция масок)
orig_draw = panel.draw
panel.draw = lambda: None  # базовая линия: UI без панели
# A/B чередуется 3 раза, берём минимум с каждой стороны: одиночный замер
# ловит GC/планировщик (+5 мс шума на ~40 мс кадре) и даёт ложный FAIL.
without_best = float("inf")
with_best = float("inf")
for _ in range(3):
    without_best = min(without_best, run_frames(30, frame_rgb))
    panel.draw = orig_draw
    with_best = min(with_best, run_frames(30, frame_rgb))
    panel.draw = lambda: None
panel.draw = orig_draw
delta_ms = with_best - without_best
print(f"    loop: with panel {with_best:.2f} ms/frame, without "
      f"{without_best:.2f} ms/frame (минимум из 3x30, incl. clock.tick(60))")
check(f"panel adds only {delta_ms:+.2f} ms/frame (< 2.0, min of 3x30)",
      delta_ms < 2.0)

# Pi-режим: свежий статус
U.set_video_source("pi", "10.192.226.51")
U.set_publish_state(True, "bound")
U.note_colors_sent()
U.set_pi_status(SP.parse_status_payload(json.dumps(good).encode()))
U.main_loop_frame(frame_rgb)
panel.draw()
pygame_shot = os.path.join(out_dir, "status_panel_preview_pi.png")
pygame.image.save(
    U.screen.subsurface(pygame.Rect(panel.rect)),
    pygame_shot,
)
print(f"    saved {pygame_shot}")

# локальный режим: слот 0 настроен, трекинг должен найти жёлтый кусок
U.set_video_source("local", "0")
U.set_pi_status(None)
U.main_loop_frame(frame_rgb)
found0 = [r for r in U.track_results if r["slot"] == 0 and r["found"]]
check("local tracking finds yellow blob (slot 0)", len(found0) == 1,
      f"angle={found0[0]['angle'] if found0 else '-'}")
panel.draw()
pygame.image.save(U.screen.subsurface(pygame.Rect(panel.rect)),
                  os.path.join(out_dir, "status_panel_preview_local.png"))
print("    saved status_panel_preview_local.png")

check("ui_fps measured > 0", panel.ui_fps > 0.0, f"{panel.ui_fps:.1f}")

# ============ E. micro-cost of the additions ============
print("=== E. added per-frame cost ===")
n = 1000
t0 = time.perf_counter()
for _ in range(n):
    R.build_gate_status(tracker, gate_infos, 25.9)
build_ms = (time.perf_counter() - t0) / n * 1000
t0 = time.perf_counter()
payload = json.dumps(st, separators=(",", ":")).encode()
for _ in range(n):
    SP.parse_status_payload(payload)
parse_ms = (time.perf_counter() - t0) / n * 1000
t0 = time.perf_counter()
for _ in range(300):
    panel.draw()
draw_ms = (time.perf_counter() - t0) / 300 * 1000
print(f"    build_gate_status {build_ms:.4f} ms | parse_status_payload "
      f"{parse_ms:.4f} ms | panel.draw {draw_ms:.3f} ms")
check("added cost negligible (< 2 ms/frame total)",
      build_ms + parse_ms + draw_ms < 2.0)

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
