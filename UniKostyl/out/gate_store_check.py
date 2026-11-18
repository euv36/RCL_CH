# -*- coding: utf-8 -*-
"""out/gate_store_check.py — camera-free verification of Idea 3 (no hardware).

Covers (PROMPT_EN.md §6, PC-first):
  A. gate_store units: load tolerance (missing/corrupt/wrong version),
     round-trip save->load, upsert copy-on-write + unknown fields kept
     (Idea 9 role survives), id range 1..3, clamp, lab_configured.
  B. Atomic writes under simulated crash: leftover tmp ignored + cleaned;
     os.replace failing mid-save leaves the old file intact and valid JSON.
  C. Pi side (rpi_elp_cam.py, no camera): "Pi restart without laptop" —
     fresh GateStore -> restore_rgb -> GateTracker == live-color state
     (parity incl. hue wraparound), synthetic-frame detection works right
     after restore; duplicate colors don't rewrite the file (debounce).
  D. Real RGBListener over ZMQ: colors arrive on a 5557-like port -> a
     FRESH tracker is enabled AND the saved_gates.json copy is updated
     (legacy {"rgb":...} too; laptop LAB thresholds survive Pi updates).
  E. Laptop side: cv_kostyl restore block replicated against a real
     unikostyl (SDL dummy driver): thresholds + set_thr_to_sliders survive
     main_loop_frame slider write-back; SAVE-persist handler replication;
     build_gate_payload protocol shape; edited cv_kostyl imports cleanly.
  F. ZMQ round-trip of the restored-colors payload (auto-send format).
  G. Per-frame cost of the additions (flush_if_due etc.).

Run:  python out\\gate_store_check.py   (from UniKostyl/, system Python)
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


TMP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gate_store_check_tmp")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)
PATH = os.path.join(TMP, "saved_gates.json")

YELLOW = (255, 255, 0)
BLUE = (0, 0, 255)
RED = (255, 0, 0)
LAB_YELLOW = [40, 90, -40, 10, 80, 127]  # не стоковый
LAB_BLUE = [30, 80, 10, 60, -110, -40]

# ============ A. gate_store units ============
print("=== A. gate_store units ===")
check("load missing file -> {}", GS.load_gates(PATH) == {})

store = GS.GateStore(path=PATH)  # пусто на старте
check("GateStore on missing file: empty + no error", store.gates == {})

g = GS.upsert({}, 1, rgb=YELLOW, lab=LAB_YELLOW)
g1_snapshot = json.dumps(g, sort_keys=True)
g2 = GS.upsert(g, 2, rgb=BLUE, lab=None)  # Pi-стиль: без LAB
check("upsert copy-on-write: new dict, old untouched",
      g2 is not g and json.dumps(g, sort_keys=True) == g1_snapshot)
g2 = GS.upsert(g2, 3, rgb=(12, 34, 56), lab=None, enabled=False)
check("gate 2 record has rgb, lab None, enabled True",
      g2["2"]["rgb"] == list(BLUE) and g2["2"]["lab"] is None
      and g2["2"]["enabled"] is True)
check("gate 3 disabled", g2["3"]["enabled"] is False)
out4 = GS.upsert(g2, 4, rgb=(1, 2, 3))
check("id 4 rejected (returns unchanged)", out4 is g2 and "4" not in out4)
bad = GS.upsert(g2, 1, rgb=(999, -1, "x"))
check("invalid rgb kept previous, warned", bad["1"]["rgb"] == list(YELLOW))

# неизвестное поле (Idea 9 добавит role) должно пережить перезапись
g2["1"]["role"] = "ours"
g3 = GS.upsert(g2, 2, rgb=BLUE)
check("unknown field 'role' survives upsert of ANOTHER gate",
      g3["1"].get("role") == "ours")

ok = GS.save_gates(PATH, g3)
check("save_gates returns True", ok)
loaded = GS.load_gates(PATH)
check("round-trip: gate 1 rgb+lab", loaded["1"]["rgb"] == list(YELLOW)
      and loaded["1"]["lab"] == LAB_YELLOW)
check("round-trip: gate 2 lab None", loaded["2"]["lab"] is None)
check("round-trip: gate 3 enabled False", loaded["3"]["enabled"] is False)
check("round-trip: unknown field kept in FILE", loaded["1"].get("role") == "ours")
check("round-trip: ts is non-empty string",
      isinstance(loaded["1"].get("ts"), str) and loaded["1"]["ts"] != "")

check("lab_configured: default range -> False",
      GS.lab_configured([0, 100, -128, 127, -128, 127]) is False)
check("lab_configured: real range -> True", GS.lab_configured(LAB_YELLOW) is True)
check("clamp_lab out-of-range clamped",
      GS.clamp_lab([-5, 200, 0, 300, -400, 9]) == [0, 100, 0, 127, -128, 9])
check("clamp_lab junk -> None", GS.clamp_lab("bad") is None)
check("iter_enabled_rgb skips disabled gate 3",
      GS.iter_enabled_rgb(loaded) == [(1, YELLOW), (2, BLUE)])

# ============ B. atomic write under simulated crash ============
print("=== B. atomic writes / simulated crash ===")
# B1: crash before replace — tmp остался, читается старый файл
with open(PATH + ".tmp.4242", "w", encoding="utf-8") as f:
    f.write("{corrupt garbage")
loaded = GS.load_gates(PATH)
check("stale tmp file ignored by load", loaded.get("1", {}).get("rgb") == list(YELLOW))
GS.save_gates(PATH, loaded)
check("next save cleans stale tmp", not any(
    n.endswith(".tmp.4242") for n in os.listdir(TMP)))

# B2: os.replace падает посреди записи — старый файл цел и валиден
real_replace = os.replace
try:
    os.replace = lambda a, b: (_ for _ in ()).throw(OSError("simulated crash"))
    ok = GS.save_gates(PATH, loaded)
finally:
    os.replace = real_replace
check("save with failing replace returns False", ok is False)
reloaded = GS.load_gates(PATH)
check("old file intact after failed save (valid JSON, gate 1 yellow)",
      reloaded.get("1", {}).get("rgb") == list(YELLOW))
check("no tmp left after failed save", not [n for n in os.listdir(TMP)
                                            if ".tmp." in n])

# B3: коррупт основного файла -> {} + WARN, не исключение
with open(PATH, "w", encoding="utf-8") as f:
    f.write('{"version": 1, "gates": {"1": {"rgb": [1,2,3')  # обрыв посреди
check("corrupt file -> {} (no exception)", GS.load_gates(PATH) == {})
with open(PATH, "w", encoding="utf-8") as f:
    json.dump({"version": 99, "gates": {}}, f)
check("wrong version -> {}", GS.load_gates(PATH) == {})
with open(PATH, "w", encoding="utf-8") as f:
    json.dump({"version": 1, "gates": {"1": {"rgb": "bad"}, "junk": "x"}}, f)
check("records without rgb AND lab dropped; junk ids skipped",
      GS.load_gates(PATH) == {})

# ============ C. Pi side: restart without laptop ============
print("=== C. Pi side: GateTracker restore parity (no camera) ===")
import rpi_elp_cam as R

store = GS.GateStore(path=PATH)
store.set_rgb(1, YELLOW, lab=LAB_YELLOW)
store.set_rgb(2, BLUE)
store.flush_pending()
check("set_rgb wrote through (flush_pending no-op after write)",
      store.flush_pending() is False)

# «Pi перезапустился»: свежий GateStore из файла -> трекер
restored_store = GS.GateStore(path=PATH)
tracker = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
restored_ids = [gid for gid, _ in restored_store.restore_rgb()]
for gid, rgb in restored_store.restore_rgb():
    tracker.update_gate_from_rgb(gid - 1, rgb)
check("restore raises gates 1,2 (gate 3 disabled in file)",
      restored_ids == [1, 2])
check("restored tracker: gates 1,2 enabled, gate 3 not",
      tracker.params[0]["enabled"] and tracker.params[1]["enabled"]
      and not tracker.params[2]["enabled"])

# паритет: восстановление из файла == приём того же цвета «вживую»
live = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
live.update_gate_from_rgb(0, YELLOW)
live.update_gate_from_rgb(1, BLUE)
same_state = all(
    np.array_equal(tracker.lowers[i], live.lowers[i])
    and np.array_equal(tracker.uppers[i], live.uppers[i])
    for i in (0, 1)
)
check("restored HSV ranges == live-color HSV ranges", same_state)

# hue wrap: красный (H=0) — восстановление даёт те же 2 диапазона
store.set_rgb(3, RED)
restored_store2 = GS.GateStore(path=PATH)
tr_wrap = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
for gid, rgb in restored_store2.restore_rgb():
    tr_wrap.update_gate_from_rgb(gid - 1, rgb)
live_wrap = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)
live_wrap.update_gate_from_rgb(2, RED)
check("hue wrap 0/179: restored ranges == live (2 H-ranges)",
      len(tr_wrap.params[2]["h_ranges"]) == 2
      and np.array_equal(tr_wrap.lowers[2], live_wrap.lowers[2])
      and np.array_equal(tr_wrap.uppers[2], live_wrap.uppers[2]))

# ворота активны сразу после рестарта: синтетический кадр -> найдены
frame = np.full((360, 640, 3), 40, np.uint8)      # BGR фон
frame[100:160, 200:300] = (0, 255, 255)           # жёлтый кусок
frame[250:300, 400:470] = (255, 0, 0)             # синий кусок
import cv2
cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
info1 = tracker.process(0)
info2 = tracker.process(1)
check("right after restart gate 1 FOUND on synthetic frame",
      info1 is not None and info1["angle"] > 0 and info1["area"] > 0)
check("right after restart gate 2 FOUND",
      info2 is not None and info2["angle"] > 0)

# ============ C2. debounce + duplicate suppression (Pi) ============
print("=== C2. Pi-side debounce (min_interval=1.0) ===")
P2 = os.path.join(TMP, "saved_gates_pi.json")
store2 = GS.GateStore(path=P2, min_interval=1.0)
w1 = store2.set_rgb(1, YELLOW)
rec_after_first = GS.load_gates(P2).get("1")
w2 = store2.set_rgb(1, YELLOW)  # дубль (автоотправка ноутбука)
check("first set_rgb writes immediately (interval elapsed)", w1 is True)
check("duplicate same color -> False, no write", w2 is False)
check("ts not churned by duplicate",
      GS.load_gates(P2)["1"]["ts"] == rec_after_first["ts"])
w3 = store2.set_rgb(1, (10, 200, 30))  # новый цвет сразу после записи
check("new color within interval -> deferred (False, dirty)", w3 is False)
w4 = store2.flush_if_due()  # ещё не прошло 1 c
check("flush_if_due before interval -> still deferred", w4 is False)
time.sleep(1.05)
w5 = store2.flush_if_due()
check("flush_if_due after interval writes deferred change", w5 is True)
check("deferred change landed in file",
      GS.load_gates(P2)["1"]["rgb"] == [10, 200, 30])

# ============ D. real RGBListener over ZMQ (5557 pattern) ============
print("=== D. RGBListener thread: colors arrive -> file updated ===")
P3 = os.path.join(TMP, "saved_gates_listener.json")
lst_store = GS.GateStore(path=P3, min_interval=0.0)
ctx = zmq.Context()
pub = ctx.socket(zmq.PUB)
pub.setsockopt(zmq.LINGER, 0)
port = pub.bind_to_random_port("tcp://127.0.0.1")

# СВЕЖИЙ трекер: проверяем именно, что ворота поднимаются с провода
tracker2 = R.GateTracker(640, 360, (40, 100, 100), 8, 1.0, 3)


class _StubStreamer:
    def set_enabled(self, v):
        pass


listener = R.RGBListener(ctx, "127.0.0.1", port,
                         tracker2, _StubStreamer(), lst_store)
listener.start()
time.sleep(0.4)  # SUB connect

payload = json.dumps({"gates": [{"id": 1, "rgb": list(YELLOW)},
                                {"id": 2, "rgb": list(BLUE)}]})
got_gates = False
for _ in range(40):
    pub.send_string(payload)
    if tracker2.params[0]["enabled"] and tracker2.params[1]["enabled"]:
        got_gates = True
        break
    time.sleep(0.05)
check("listener enabled gates 1,2 from wire (fresh tracker)", got_gates)
frec = GS.load_gates(P3)
check("listener updated saved_gates.json copy (gates 1,2)",
      frec.get("1", {}).get("rgb") == list(YELLOW)
      and frec.get("2", {}).get("rgb") == list(BLUE))

pub.send_string(json.dumps({"rgb": [200, 30, 40]}))  # legacy gate 1
time.sleep(0.3)
frec2 = GS.load_gates(P3)
check("legacy {'rgb':...} updates gate 1 in file",
      frec2.get("1", {}).get("rgb") == [200, 30, 40])
check("legacy update did not touch gate 2",
      frec2.get("2", {}).get("rgb") == list(BLUE))

# LAB-пороги, сохранённые ноутбуком, Pi не трогает (rgb-only update)
P4 = os.path.join(TMP, "saved_gates_pi_keep_lab.json")
keep_store = GS.GateStore(path=P4)
keep_store.set_rgb(1, YELLOW, lab=LAB_YELLOW)
keep_store2 = GS.GateStore(path=P4)
keep_store2.set_rgb(1, [250, 250, 10])  # Pi: только rgb
kept = GS.load_gates(P4)
check("Pi rgb-only update keeps laptop's LAB thresholds",
      kept["1"]["lab"] == LAB_YELLOW and kept["1"]["rgb"] == [250, 250, 10])

listener.stop()
listener.join(timeout=1.5)

# ============ E. laptop side: unikostyl restore + SAVE persist ============
print("=== E. laptop: unikostyl restore + SAVE persist (SDL dummy) ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U
import cv_kostyl as CK  # отредактированный модуль должен импортироваться

PL = os.path.join(TMP, "saved_gates_laptop.json")
lp_store = GS.GateStore(path=PL)
lp_store.set_rgb(1, YELLOW, lab=LAB_YELLOW)
lp_store.set_rgb(2, BLUE, lab=LAB_BLUE)
lp_store.set_rgb(3, RED, lab=None)  # lab null: слот не восстанавливается

# --- репликация блока восстановления из cv_kostyl.main_loop() ---
orig_thresholds = [list(t) for t in U.thresholds]
restored_gates = {}
for gate_id, rgb in lp_store.restore_rgb():
    restored_gates[gate_id] = rgb
    lab = lp_store.get_lab(gate_id)
    if lab is not None and GS.lab_configured(lab):
        U.thresholds[gate_id - 1] = list(lab)  # копия! не ссылка из store
if restored_gates:
    U.set_thr_to_sliders()
check("restore: slots 1,2,3 colors ready", sorted(restored_gates) == [1, 2, 3])
check("restore: slot 1 LAB restored", U.thresholds[0] == LAB_YELLOW)
check("restore: slot 2 LAB restored", U.thresholds[1] == LAB_BLUE)
check("restore: slot 3 (lab null) NOT restored",
      U.thresholds[2] == orig_thresholds[2])
check("restore: slot 3 color still auto-sent", 3 in restored_gates)
check("slot_configured(restored slot 1)", U.slot_configured(U.thresholds[0]))

# слайдер-возврат в main_loop_frame не должен затирать восстановленный слот 1
frame_rgb = np.zeros((360, 640, 3), np.uint8)
frame_rgb[:, :, 0] = 120
for _ in range(5):
    U.main_loop_frame(frame_rgb)
check("restored slot 1 survives slider write-back (5 frames)",
      U.thresholds[0] == LAB_YELLOW)
track1 = [r for r in U.track_results if r["slot"] == 0]
check("restored slot 1 tracked as enabled by UI",
      bool(track1) and track1[0]["enabled"] is True)

# --- репликация SAVE-обработчика из cv_kostyl.py ---
U.thresholds[0] = [50, 95, -30, 5, 70, 120]  # пользователь перекалибровал
mid = ((50 + 95) / 2, (-30 + 5) / 2, (70 + 120) / 2)
rgb_mid = tuple(int(c) for c in U.lab_to_rgb(*mid))
pending = {1: rgb_mid}
for gate_id in sorted(pending):
    if not (1 <= gate_id <= 3):
        continue
    lp_store.set_rgb(gate_id, pending[gate_id],
                     lab=list(map(int, U.thresholds[gate_id - 1])))
saved_after_save = GS.load_gates(PL)
check("SAVE Gn overwrites slot n (rgb+lab)",
      saved_after_save["1"]["rgb"] == list(rgb_mid)
      and saved_after_save["1"]["lab"] == [50, 95, -30, 5, 70, 120])
check("SAVE Gn kept other slots (slot 2 lab intact)",
      saved_after_save["2"]["lab"] == LAB_BLUE)
# и восстановление после «рестарта приложения» даёт новый цвет
lp_store2 = GS.GateStore(path=PL)
check("restart: new slot-1 color restored",
      (1, rgb_mid) in lp_store2.restore_rgb()
      and lp_store2.get_lab(1) == [50, 95, -30, 5, 70, 120])

payload_obj = CK.build_gate_payload(restored_gates)
ok_shape = (payload_obj.get("rgb") == list(YELLOW)
            and {"id": 1, "rgb": list(YELLOW)} in payload_obj["gates"]
            and {"id": 2, "rgb": list(BLUE)} in payload_obj["gates"]
            and {"id": 3, "rgb": list(RED)} in payload_obj["gates"])
check("build_gate_payload(restored) protocol shape", ok_shape)

# ============ F. ZMQ round-trip of auto-send payload ============
print("=== F. ZMQ round-trip: restored colors to a Pi-simulated SUB ===")
sub = ctx.socket(zmq.SUB)
sub.setsockopt_string(zmq.SUBSCRIBE, "")
sub.setsockopt(zmq.LINGER, 0)
sub.setsockopt(zmq.RCVTIMEO, 300)
sub.connect(f"tcp://127.0.0.1:{port}")
time.sleep(0.35)
received = None
for _ in range(20):
    pub.send_string(json.dumps(CK.build_gate_payload(restored_gates)))
    try:
        received = json.loads(sub.recv_string())
        break
    except zmq.Again:
        continue
check("restored payload received over ZMQ", received is not None)
if received:
    ids = {g["id"]: g["rgb"] for g in received.get("gates", [])}
    check("wire payload carries gates 1..3 with rgb",
          ids == {1: list(YELLOW), 2: list(BLUE), 3: list(RED)})
    check("legacy rgb field = gate 1", received.get("rgb") == list(YELLOW))
sub.close()
pub.close()
ctx.term()

# ============ G. cost of additions ============
print("=== G. per-frame cost of additions ===")
n = 200000
t0 = time.perf_counter()
for _ in range(n):
    lst_store.flush_if_due()
flush_ms = (time.perf_counter() - t0) / n * 1000
t0 = time.perf_counter()
for _ in range(n):
    GS.iter_enabled_rgb(lst_store.gates)
iter_ms = (time.perf_counter() - t0) / n * 1000
print(f"    flush_if_due {flush_ms:.5f} ms | restore_rgb(iter) {iter_ms:.5f} ms")
check("additions negligible per frame (< 0.05 ms)",
      flush_ms < 0.05 and iter_ms < 0.05)

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
