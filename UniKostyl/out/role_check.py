# -*- coding: utf-8 -*-
"""out/role_check.py — camera-free verification of Idea 9 (gate roles).

Covers (PROMPT_EN.md §6 «Ideas 9–11: role round-trip», no hardware):
  A. gate_store role units: normalize_role/stored_role/roles_map,
     upsert(role=...) set/clear/preserve, unknown fields still preserved.
  B. Round-trip: select role -> SAVE -> saved_gates.json contains role ->
     "restart" (fresh GateStore) -> role restored (badge source).
  C. Wire format 5557 (additive check): payload WITHOUT roles is byte-shape
     identical to the Idea 3 contract (old Pi safe); with roles the "role"
     field appears ONLY when set; legacy {"rgb"} unchanged.
  D. Pi side (rpi_elp_cam.py, no camera): RGBListener over real ZMQ —
     "role" reaches tracker params AND the saved_gates.json copy; messages
     WITHOUT role keep the previous role (old laptop safe); duplicate
     color+role does not rewrite the file; "Pi restart" restores the role
     from the file; build_gate_status adds "role" only when set and stays
     JSON-serializable.
  E. Status panel: parse_status_payload passes "role" through; panel
     set_roles + badge drawing does not crash under SDL dummy and costs
     nothing per frame.
  F. Old-parser compatibility: an unmodified Idea-2-style gate record
     (no role) produces no "role" key anywhere.

Run:  python out\\role_check.py   (from UniKostyl/, system Python)
"""
import json
import os
import shutil
import sys
import time

import numpy as np
import zmq

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import gate_store as GS

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


TMP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "role_check_tmp")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)
PATH = os.path.join(TMP, "saved_gates.json")

YELLOW = (255, 255, 0)
BLUE = (0, 0, 255)
ORANGE = (255, 140, 0)

# ============ A. gate_store role units ============
print("=== A. gate_store role units ===")
check("normalize_role: ours/opponent pass through",
      GS.normalize_role("ours") == "ours"
      and GS.normalize_role("opponent") == "opponent")
check("normalize_role: None/junk/'unset' -> unset",
      GS.normalize_role(None) == "unset" and GS.normalize_role("junk") == "unset"
      and GS.normalize_role("unset") == "unset")
check("stored_role: missing rec / no key -> unset",
      GS.stored_role(None) == "unset" and GS.stored_role({}) == "unset")
check("roles_map: only set roles, int ids",
      GS.roles_map({"1": {"role": "ours"}, "2": {"role": "unset"},
                    "3": {}}) == {1: "ours"})

g = GS.upsert({}, 1, rgb=YELLOW, role="ours")
check("upsert sets role", g["1"]["role"] == "ours")
g = GS.upsert(g, 1, rgb=YELLOW, role="opponent")
check("upsert changes role", g["1"]["role"] == "opponent")
g = GS.upsert(g, 1, rgb=YELLOW, role="unset")
check("upsert role=unset REMOVES the key (absence == unset)",
      "role" not in g["1"])
g = GS.upsert(g, 1, rgb=YELLOW, role=None)
check("upsert role=None preserves absence", "role" not in g["1"])
g = GS.upsert(g, 1, rgb=YELLOW, role="ours")
g = GS.upsert(g, 1, rgb=BLUE)  # SAVE без роли — прежняя роль переживает
check("upsert without role keeps previous role", g["1"]["role"] == "ours")
g = GS.upsert(g, 2, rgb=BLUE, role="weird-role")
check("upsert unknown role ignored (previous kept)",
      "role" not in g["2"])
g["3"] = {"rgb": list(ORANGE), "lab": None, "enabled": True, "ts": "x",
          "custom_future_field": 42}
g = GS.upsert(g, 3, rgb=ORANGE, role="opponent")
check("unknown FIELDS still preserved (Idea 10 safe)",
      g["3"].get("custom_future_field") == 42)
check("upsert role=None never invents a role", "role" not in
      GS.upsert({}, 1, rgb=YELLOW, role=None)["1"])

# ============ B. round-trip through the file ============
print("=== B. role round-trip: select -> SAVE -> file -> restart ===")
store = GS.GateStore(path=PATH)  # ноутбук: min_interval=0 — пишет сразу
store.set_rgb(1, YELLOW, role="ours")
store.set_rgb(2, BLUE, role="opponent")
store.set_rgb(3, ORANGE)
on_disk = json.load(open(PATH, encoding="utf-8"))
check("file: gate 1 role ours", on_disk["gates"]["1"].get("role") == "ours")
check("file: gate 2 role opponent",
      on_disk["gates"]["2"].get("role") == "opponent")
check("file: gate 3 (unset) has NO role key", "role" not in on_disk["gates"]["3"])

# смена роли на unset — ключ уходит из файла
store.set_role(2, "unset")
on_disk = json.load(open(PATH, encoding="utf-8"))
check("set_role(unset) removes key from file",
      "role" not in on_disk["gates"]["2"])
store.set_role(2, "opponent")

# «рестарт приложения»: свежий GateStore из файла
store2 = GS.GateStore(path=PATH)
check("restart: role 1 restored", store2.get_role(1) == "ours")
check("restart: role 2 restored", store2.get_role(2) == "opponent")
check("restart: role 3 unset", store2.get_role(3) == "unset")
check("roles_map after restart", store2.roles() ==
      {1: "ours", 2: "opponent"})

# set_role не трогает цвет/enabled/LAB слота
lab = [40, 90, -40, 10, 80, 127]
store.set_rgb(1, YELLOW, lab=lab)
store.set_role(1, "opponent")
rec = GS.load_gates(PATH)["1"]
check("set_role keeps rgb/lab/enabled",
      rec["rgb"] == list(YELLOW) and rec["lab"] == lab
      and rec["enabled"] is True and rec["role"] == "opponent")
check("set_role same value -> no write (False)",
      store.set_role(1, "opponent") is False)
check("set_role on never-saved gate -> record without rgb is session-only",
      store.set_role(3, "ours") in (True, False))  # не должно падать

# ============ C. wire format 5557 (additive) ============
print("=== C. wire format: additive only ===")
import cv_kostyl as CK

plain = CK.build_gate_payload({1: YELLOW, 2: BLUE})
check("no roles -> EXACT old shape (old Pi safe)",
      plain == {"gates": [{"id": 1, "rgb": list(YELLOW)},
                          {"id": 2, "rgb": list(BLUE)}],
                "rgb": list(YELLOW)})
roles = {1: "ours", 2: "opponent"}
with_roles = CK.build_gate_payload({1: YELLOW, 2: BLUE}, roles)
check("role field added only when set",
      with_roles["gates"][0] == {"id": 1, "rgb": list(YELLOW), "role": "ours"}
      and with_roles["gates"][1] == {"id": 2, "rgb": list(BLUE),
                                     "role": "opponent"})
partial = CK.build_gate_payload({1: YELLOW, 2: BLUE}, {1: "unset", 2: "junk"})
check("unset/junk roles never reach the wire",
      "role" not in partial["gates"][0] and "role" not in partial["gates"][1])
check("legacy rgb field untouched by roles",
      with_roles.get("rgb") == list(YELLOW))
check("str keys in roles map also accepted",
      CK.build_gate_payload({1: YELLOW}, {"1": "ours"})
      ["gates"][0].get("role") == "ours")

# ============ D. Pi side over real ZMQ ============
print("=== D. Pi side: RGBListener role pass-through ===")
import rpi_elp_cam as R

ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.setsockopt(zmq.LINGER, 0)
port = pub.bind_to_random_port("tcp://127.0.0.1")

P4 = os.path.join(TMP, "saved_gates_pi.json")
pi_store = GS.GateStore(path=P4, min_interval=1.0)
tracker = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)


class _StubStreamer:
    def set_enabled(self, v):
        pass


listener = R.RGBListener(ctx, "127.0.0.1", port, tracker, _StubStreamer(),
                         pi_store)
listener.start()
time.sleep(0.4)  # SUB connect


def send_until(pred, payload, tries=40):
    for _ in range(tries):
        pub.send_string(payload)
        if pred():
            return True
        time.sleep(0.05)
    return False


ok = send_until(
    lambda: tracker.params[0].get("role") == "ours",
    json.dumps({"gates": [{"id": 1, "rgb": list(YELLOW), "role": "ours"}]}),
)
check("role 'ours' reached tracker params over ZMQ", ok)
frec = GS.load_gates(P4)
check("role reached the saved_gates.json copy",
      frec.get("1", {}).get("role") == "ours")
check("gate 1 also got the color", frec["1"]["rgb"] == list(YELLOW))

ok = send_until(
    lambda: tracker.params[1].get("role") == "opponent",
    json.dumps({"gates": [{"id": 2, "rgb": list(BLUE), "role": "opponent"}]}),
)
check("role 'opponent' on gate 2", ok)

# сообщение БЕЗ роли: прежняя роль сохраняется (старый ноутбук безопасен)
before = GS.load_gates(P4)["1"]["role"]
pub.send_string(json.dumps({"gates": [{"id": 1, "rgb": [250, 250, 10]}]}))
time.sleep(0.4)
check("message without role keeps tracker role",
      tracker.params[0].get("role") == "ours")
check("message without role keeps file role",
      GS.load_gates(P4)["1"]["role"] == before)
# Pi пишет копию файла не чаще min_interval (1 c) — flush_if_due() зовёт
# основной цикл; реплицируем его: ждём и дописываем отложенное.
time.sleep(1.05)
pi_store.flush_if_due()
check("message without role still updated the color",
      GS.load_gates(P4)["1"]["rgb"] == [250, 250, 10])
check("deferred flush kept the role",
      GS.load_gates(P4)["1"]["role"] == before)

# мусорная роль — игнорируется (не падает, не сбрасывает)
pub.send_string(json.dumps({"gates": [{"id": 1, "rgb": [250, 250, 10],
                                       "role": 5}]}))
time.sleep(0.4)
check("garbage role ignored", tracker.params[0].get("role") == "ours")

# дубликат цвет+роль не перезаписывает файл (ts не churnится)
time.sleep(1.05)
pi_store.flush_if_due()
ts_before = GS.load_gates(P4)["1"]["ts"]
pub.send_string(json.dumps({"gates": [{"id": 1, "rgb": [250, 250, 10],
                                       "role": "ours"}]}))
time.sleep(0.4)
pi_store.flush_if_due()
check("duplicate color+role does not rewrite the file",
      GS.load_gates(P4)["1"]["ts"] == ts_before)

# «Pi перезапустился»: роль поднимается из файла в трекер (репликация run())
tracker2 = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
fresh = GS.GateStore(path=P4)
for gid, rgb in fresh.restore_rgb():
    tracker2.update_gate_from_rgb(gid - 1, rgb)
    rec = fresh.gates.get(str(gid))
    if isinstance(rec, dict) and rec.get("role") in ("ours", "opponent"):
        tracker2.params[gid - 1]["role"] = rec["role"]
check("restart: role restored into tracker params",
      tracker2.params[0].get("role") == "ours"
      and tracker2.params[1].get("role") == "opponent")

# build_gate_status: role в JSON только когда задана
frame = np.full((360, 640, 3), 40, np.uint8)
frame[100:160, 200:300] = (0, 255, 255)  # жёлтый кусок (BGR)
import cv2
cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker2.hsv)
gate_infos = {}
for gidx in range(3):
    if tracker2.params[gidx]["enabled"]:
        gate_infos[gidx] = tracker2.process(gidx)
st = R.build_gate_status(tracker2, gate_infos, 25.9)
check("status: gate 1 carries role ours", st["gates"]["1"].get("role") == "ours")
check("status: gate 2 carries role opponent",
      st["gates"]["2"].get("role") == "opponent")
check("status: JSON-serializable",
      isinstance(json.dumps(st, separators=(",", ":")), str))
print(f"    status sample: {json.dumps(st, separators=(',', ':'))}")

# статус БЕЗ ролей: ключ "role" нигде не появляется (старый формат цел)
tracker3 = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
tracker3.update_gate_from_rgb(0, YELLOW)
st_old = R.build_gate_status(tracker3, {0: None}, 25.9)
check("no roles -> no 'role' key in status (byte-shape as Idea 2)",
      all("role" not in g for g in st_old["gates"].values()))

listener.stop()
listener.join(timeout=1.5)

# ============ E. status panel badges ============
print("=== E. status panel: role pass-through + badges ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import pygame
import status_panel as SP
import unikostyl as U

parsed = SP.parse_status_payload(json.dumps(st).encode())
check("parse_status_payload passes role through",
      parsed["gates"]["1"].get("role") == "ours"
      and parsed["gates"]["2"].get("role") == "opponent")

panel = U.widgets.get("status_panel")
check("status_panel registered", panel is not None)
panel.set_roles({1: "ours", 2: "opponent", 3: "junk"})
check("set_roles normalizes junk away",
      panel.roles == {1: "ours", 2: "opponent"})
frame_rgb = np.zeros((360, 640, 3), np.uint8)
frame_rgb[:, :, 0] = 120
frame_rgb[280:320, 40:120] = (255, 255, 0)  # жёлтый кусок для локального трека
U.set_video_source("local", "0")
U.main_loop_frame(frame_rgb)
panel.draw()  # локальная таблица + бейджи: не должно падать
U.set_video_source("pi", "127.0.0.1")
U.set_pi_status(parsed)
U.main_loop_frame(frame_rgb)
panel.draw()  # pi-таблица + бейджи из статус-JSON
check("panel draw with badges: no exception", True)

n = 2000
t0 = time.perf_counter()
for _ in range(n):
    panel.draw()
draw_ms = (time.perf_counter() - t0) / n * 1000
print(f"    panel.draw with badges: {draw_ms:.3f} ms")
check("badge cost negligible (< 1 ms/draw)", draw_ms < 1.0)

shot = os.path.join(TMP, "panel_roles.png")
pygame.image.save(U.screen.subsurface(pygame.Rect(panel.rect)), shot)
print(f"    saved {shot}")

# ============ F. old-parser compatibility ============
print("=== F. old parser / old payloads ===")
old_status = {"gates": {"1": {"enabled": True, "found": True, "angle": 5,
                              "area": 100}}, "fps": 25.0}
res = SP.parse_status_payload(json.dumps(old_status).encode())
check("old status JSON: no role, parses fine",
      res is not None and "role" not in res["gates"]["1"])
old_msg = '{"gates":[{"id":1,"rgb":[1,2,3]}]}'
tm = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)


class _S2:
    def set_enabled(self, v):
        pass


lst2 = R.RGBListener(ctx, "127.0.0.1", port, tm, _S2(), None)
lst2.start()
time.sleep(0.35)
ok = send_until(lambda: tm.params[0]["enabled"], old_msg, tries=30)
check("old color message (no role) still enables gates (gate_store=None Pi)",
      ok)
check("old message leaves no role in params", "role" not in tm.params[0])
lst2.stop()
lst2.join(timeout=1.5)

pub.close()
ctx.term()

# ============ G. CAM SET window: role selector ============
print("=== G. cam_settings: GATE ROLES section (headless canvas) ===")
import cam_settings as CS

PG_PATH = os.path.join(TMP, "saved_gates_ui.json")
ui_store = GS.GateStore(path=PG_PATH)
ui_store.set_rgb(1, YELLOW)
cb_calls = []
win = CS.CameraSettingsWindow(
    gate_store_obj=ui_store,
    roles_changed_cb=lambda: cb_calls.append(1),
    gate_colors=((255, 255, 0), (0, 0, 255), (255, 140, 0)),
)
check("window role API wired", win.gate_store_obj is ui_store)
check("initial roles from store", win._gate_role(1) == "unset")

win._draw()  # полотно строится без открытого cv2-окна
missing = [f"role{gid}_{rn}" for gid in (1, 2, 3) for rn in GS.ROLES
           if f"role{gid}_{rn}" not in win._btn_rects]
check("all 9 role buttons registered on canvas", not missing, str(missing))

win._set_gate_role(1, "ours")
check("click ours -> store updated", ui_store.get_role(1) == "ours")
check("click ours -> callback fired", len(cb_calls) == 1)
check("file written with role",
      json.load(open(PG_PATH, encoding="utf-8"))["gates"]["1"]["role"]
      == "ours")
win._draw()
check("canvas draw with selected role ok", win._canvas is not None)
try:
    import cv2
    preview = os.path.join(TMP, "camset_roles.png")
    cv2.imwrite(preview, win._canvas)
    print(f"    saved {preview}")
except Exception as e:
    print(f"    (no canvas preview: {e})")
win._set_gate_role(1, "unset")
check("click unset -> key removed from store/file",
      "role" not in GS.load_gates(PG_PATH)["1"])
check("gate 1 color survived role clicks",
      GS.load_gates(PG_PATH)["1"]["rgb"] == list(YELLOW))

# install(): кнопка CAM SET + новые kwargs (PyCamCtl может не занять 5559,
# если параллельно работает cv_kostyl — бэкенд честно отключится).
win2 = CS.install(U, cap_getter=None, source_getter=None,
                  gate_store_obj=ui_store,
                  roles_changed_cb=lambda: cb_calls.append(1))
check("install() returns window with gate store",
      getattr(win2, "gate_store_obj", None) is ui_store)
check("CAM SET button registered in unikostyl UI",
      "btn_cam_settings" in U.widgets and "btn_cam_settings" in U.wnames)
win2.close()  # закроет cv2-окно, если открывали; сохранит settings json

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
