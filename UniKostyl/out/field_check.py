# -*- coding: utf-8 -*-
"""out/field_check.py — camera-free verification of Idea 10 (field binding).

Covers (PROMPT_EN.md §5 Idea 10 / §6, no hardware):
  A. field_map units: snapshot_from_results (found/held/not-found/
     unconfigured), snapshot_from_pi_status (dist_reliable=False -> dist
     null), normalize_gate_entry (angle wrap 362->2, dist clamp 0..500),
     build_map_data (preset validation, boundary + unknown keys preserved),
     estimated_directions (heading placeholder rotates, data not mutated).
  B. Atomic file round-trip on a tmp path: save -> load == equal; missing
     file -> None; corrupt garbage -> None + WARN; wrong version -> None;
     no stray .tmp files left behind.
  C. UI binding round-trip (SDL dummy, the REAL UniKostyl/field_map.json):
     simulate live tracking recs -> bind_field() -> file written atomically
     -> "restart" (load_field_map_settings) -> FIELD_MAP restored ->
     estimated directions active -> pristine file restored afterwards.
  D. Preset cycler + heading setter + preset validation; bind with no
     measured gates refuses (old file untouched).
  E. Radar: est merge in _gate_data (lost+configured -> est angle/dist;
     found -> live wins; unconfigured -> no est), dimmed arc pixel test,
     "(est)" footer, EDGE sector arcs pixel test (Idea 11 display), no
     visual change with empty feeds.
  F. Status JSON additive: parse_status_payload keeps "edge_sectors" in
     extra (schema gates/fps untouched) — never break the schema.
  G. Costs: estimated_directions < 0.05 ms, radar feed setters ~0.

Run:  python out\\field_check.py   (from UniKostyl/, system Python)
"""
import json
import math
import os
import shutil
import sys
import time

import numpy as np
import pygame

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import field_map as FM
import field_boundary as FB
import status_panel as SP
import radar_panel as RP

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.join(HERE, "field_check_tmp")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)
TMP_MAP = os.path.join(TMP, "field_map.json")

# Копия «эталонного» файла из репо для B-тестов (репозиторный не трогаем).
REPO_MAP = FM.default_path()
PRISTINE_BYTES = open(REPO_MAP, "rb").read() if os.path.exists(REPO_MAP) else None

# Страховка: какой бы ни был путь выхода (падение проверки, исключение,
# sys.exit) — файл field_map.json возвращается к состоянию на момент
# запуска. Иначе один грязный прогон делает грязными все последующие
# (PRISTINE перезаписывается уже грязными байтами — самоподдерживающийся
# сбой, наблюдался 2026-11-17).
import atexit

def _restore_pristine_map():
    if PRISTINE_BYTES is not None:
        try:
            with open(REPO_MAP, "wb") as _f:
                _f.write(PRISTINE_BYTES)
        except Exception:
            pass

atexit.register(_restore_pristine_map)

# ============ A. юниты field_map ============
print("=== A. field_map units ===")

recs = [
    {"slot": 0, "enabled": True, "found": True, "angle": 52, "dist_cm": 148.0,
     "dist_reliable": True, "r_px": 263.0, "w_px": 40, "area": 500.0},
    {"slot": 1, "enabled": True, "found": True, "angle": 121, "dist_cm": 90.0,
     "dist_reliable": False, "held": True},
    {"slot": 2, "enabled": False, "found": False, "angle": -1, "dist_cm": None},
]
snap = FM.snapshot_from_results(recs)
check("snapshot: found gate recorded with angle+dist",
      snap.get(1) == {"angle_deg": 52.0, "dist_cm": 148.0, "estimated": False})
check("snapshot: held gate -> estimated=True (not a fresh measurement)",
      snap.get(2, {}).get("estimated") is True and snap.get(2, {}).get("angle_deg") == 121.0)
check("snapshot: unconfigured slot skipped", 3 not in snap)

pi_snap = FM.snapshot_from_pi_status({
    "gates": {
        "1": {"enabled": True, "found": True, "angle": 10.5, "dist_cm": 130.0,
              "dist_reliable": True},
        "2": {"enabled": True, "found": True, "angle": 200, "dist_cm": 77.0,
              "dist_reliable": False},
        "3": {"enabled": False, "found": False},
    }
})
check("pi snapshot: found+reliable recorded",
      pi_snap.get(1) == {"angle_deg": 10.5, "dist_cm": 130.0, "estimated": False})
check("pi snapshot: dist_reliable=False -> dist_cm null (occlusion guard)",
      pi_snap.get(2, {}).get("dist_cm") is None
      and pi_snap.get(2, {}).get("angle_deg") == 200.0)
check("pi snapshot: disabled gate skipped", 3 not in pi_snap)
check("pi snapshot: junk status tolerated", FM.snapshot_from_pi_status(None) == {}
      and FM.snapshot_from_pi_status({"gates": "x"}) == {})

e = FM.normalize_gate_entry({"angle_deg": 362, "dist_cm": 12.0})
check("gate entry: angle wrap 362 -> 2", e is not None and abs(e["angle_deg"] - 2.0) < 1e-9)
check("gate entry: junk angle dropped", FM.normalize_gate_entry({"angle_deg": "x"}) is None)
check("gate entry: nan angle dropped",
      FM.normalize_gate_entry({"angle_deg": float("nan")}) is None)
check("gate entry: dist clamped to 0..500",
      FM.normalize_gate_entry({"angle_deg": 1, "dist_cm": 9999})["dist_cm"] == 500.0
      and FM.normalize_gate_entry({"angle_deg": 1, "dist_cm": -3})["dist_cm"] is None)

data = FM.build_map_data({1: {"angle_deg": 52.0, "dist_cm": 148.0}},
                         "own_goal", 12.0,
                         prev={"boundary": {"edge_share": 0.7}, "future_key": 1})
check("build_map_data: schema keys",
      data["version"] == 1 and data["robot_pos"] == "own_goal"
      and data["bound_at"] and isinstance(data["bound_at"], str)
      and data["heading_offset_deg"] == 12.0
      and data["gates"]["1"]["angle_deg"] == 52.0)
check("build_map_data: prev boundary + unknown keys preserved",
      data["boundary"] == {"edge_share": 0.7} and data["future_key"] == 1)
junk = FM.build_map_data({}, "bogus-preset", "nan-heading")
check("build_map_data: junk preset -> null (never invents a value)",
      junk["robot_pos"] is None and junk["heading_offset_deg"] == 0.0)

est = FM.estimated_directions(data)
check("estimated_directions: all bound gates marked est",
      est == {1: {"angle_deg": 52.0, "dist_cm": 148.0, "estimated": True}})
est_rot = FM.estimated_directions(data, heading_delta_deg=90.0)
check("estimated_directions: heading placeholder rotates (Idea 12 reserve)",
      abs((est_rot[1]["angle_deg"] - (52.0 - 90.0) % 360.0) % 360.0) < 1e-9)
check("apply_heading does not mutate the loaded data",
      data["gates"]["1"]["angle_deg"] == 52.0)
check("estimated_directions: empty binding -> {}", FM.estimated_directions(None) == {})

# ============ B. атомарный round-trip (tmp-путь) ============
print("=== B. file round-trip (tmp path) ===")
ok = FM.save_map(TMP_MAP, data)
check("save_map returns True", ok is True)
loaded = FM.load_map(TMP_MAP)
check("load_map round-trip equal",
      loaded == data, f"{loaded!r}")
check("no stray tmp files after save",
      not [n for n in os.listdir(TMP) if n.startswith("field_map.json.tmp.")])

check("missing file -> None", FM.load_map(os.path.join(TMP, "absent.json")) is None)
bad = os.path.join(TMP, "bad.json")
with open(bad, "w", encoding="utf-8") as f:
    f.write("{corrupt json!!!")
check("corrupt file -> None + WARN (never raises)",
      FM.load_map(bad) is None)
wrongver = os.path.join(TMP, "wrongver.json")
with open(wrongver, "w", encoding="utf-8") as f:
    json.dump({"version": 99, "gates": {}}, f)
check("wrong version -> None", FM.load_map(wrongver) is None)
arr = os.path.join(TMP, "arr.json")
with open(arr, "w", encoding="utf-8") as f:
    f.write("[1,2,3]")
check("non-dict root -> None", FM.load_map(arr) is None)
badgate = os.path.join(TMP, "badgate.json")
with open(badgate, "w", encoding="utf-8") as f:
    json.dump({"version": 1, "robot_pos": "weird", "gates":
               {"1": {"angle_deg": "junk"}, "9": {"angle_deg": 5}}}, f)
lg = FM.load_map(badgate)
check("invalid gate records dropped, junk robot_pos -> None (warn)",
      lg is not None and lg["gates"] == {} and lg["robot_pos"] is None)
check("load_boundary_section: corrupt/absent -> {} (defaults apply)",
      FM.load_boundary_section(os.path.join(TMP, "absent.json")) == {}
      and FM.load_boundary_section(bad) == {})
check("load_boundary_section: section read from the repo file",
      FM.load_boundary_section(REPO_MAP).get("n_sectors") == 8
      if PRISTINE_BYTES else True)

# ============ C. UI round-trip через РЕАЛЬНЫЙ UniKostyl/field_map.json ============
print("=== C. UI binding round-trip (SDL dummy, real contract file) ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U

btn_bind = U.widgets.get("btn_bind")
btn_pos = U.widgets.get("btn_pos")
check("btn_bind registered in top row (after RADAR)",
      btn_bind is not None and btn_bind.rect == pygame.Rect(663, 14, 112, 36),
      str(btn_bind.rect) if btn_bind else "")
check("btn_bind label + callback", btn_bind.label.text == "BIND FIELD"
      and btn_bind.func is U.bind_field)
check("btn_pos registered (after BIND FIELD)", btn_pos is not None
      and btn_pos.rect == pygame.Rect(785, 14, 120, 36)
      and btn_pos.func is U.cycle_robot_pos)
check("btn_pos default label", btn_pos.label.text == "POS: CENTER")

# «Живой трекинг»: ворота 1 найдены, 2 не найден, 3 слот не настроен.
U.track_results = [
    {"slot": 0, "enabled": True, "found": True, "angle": 52, "dist_cm": 148.0,
     "dist_reliable": True, "r_px": 263.0, "w_px": 40, "area": 500.0},
    {"slot": 1, "enabled": True, "found": False, "angle": -1, "dist_cm": None,
     "dist_reliable": False, "r_px": float("inf")},
    {"slot": 2, "enabled": False, "found": False, "angle": -1, "dist_cm": None,
     "dist_reliable": False, "r_px": float("inf")},
]
U.bind_pos_idx = 1  # «own_goal» выбран кнопкой POS
check("bind_field() on the REAL default path returns True", U.bind_field() is True)
real_now = FM.load_map(REPO_MAP)
check("real field_map.json: binding written (pos=own_goal, gate 1)",
      real_now is not None and real_now["robot_pos"] == "own_goal"
      and real_now["gates"].get("1", {}).get("angle_deg") == 52.0
      and real_now["gates"]["1"]["dist_cm"] == 148.0
      and real_now["gates"]["1"]["estimated"] is False)
check("real field_map.json: boundary section survived the binding",
      isinstance(real_now.get("boundary"), dict)
      and real_now["boundary"].get("n_sectors") == 8)
check("FIELD_MAP updated in-memory after bind (matches the written file)",
      U.FIELD_MAP == real_now)
check("no stray tmp files next to the real contract file",
      not [n for n in os.listdir(os.path.dirname(REPO_MAP))
           if n.startswith("field_map.json.tmp.")])

# «Рестарт»: та же загрузка, что при импорте модуля.
restored = U.load_field_map_settings()
check("restart: binding restored (est directions active)",
      U._estimated_directions().get(1, {}).get("angle_deg") == 52.0)

# Восстановить эталонный файл репо (тест не должен оставлять привязку).
if PRISTINE_BYTES is not None:
    with open(REPO_MAP, "wb") as f:
        f.write(PRISTINE_BYTES)
    U.load_field_map_settings()
    check("pristine field_map.json restored after the test",
          os.path.exists(REPO_MAP)
          and FM.load_map(REPO_MAP).get("gates") == {})

# ============ D. пресеты/отказы ============
print("=== D. preset cycler + refusals ===")
check("cycle_robot_pos: center->own_goal->manual->center",
      (U.cycle_robot_pos() or True) and U.bind_pos_idx == (1 + 1) % 3
      and btn_pos.label.text == "POS: MANUAL")
U.cycle_robot_pos()
check("cycler wraps to center + label synced",
      U.bind_pos_idx == 0 and btn_pos.label.text == "POS: CENTER")
check("set_bind_heading wraps 370 -> 10", abs(U.set_bind_heading(370) - 10.0) < 1e-9)
check("set_bind_heading junk ignored (stays 10)",
      U.set_bind_heading("x") == 10.0)
U.set_bind_heading(0.0)

# Нет измеренных ворот -> отказ, прежний файл не тронут.
U.track_results = [{"slot": 0, "enabled": False, "found": False, "angle": -1,
                    "dist_cm": None, "dist_reliable": False, "r_px": float("inf")}]
before = open(REPO_MAP, "rb").read() if PRISTINE_BYTES is not None else None
check("bind with no measured gates refuses (False)",
      U.bind_field() is False)
after = open(REPO_MAP, "rb").read() if before is not None else before
check("refused bind leaves the file untouched", before == after)
check("button feedback: fail color after refusal",
      btn_bind.label.color == U.BIND_BTN_FAIL)

# ============ E. радар: est-метки + EDGE-сектора ============
print("=== E. radar estimated marks + edge display ===")
radar = U.widgets.get("radar_panel")
radar.set_visible(True)
radar.rect = pygame.Rect(200, 80, 340, 340)
radar.on_video_source("local", "0")
radar.set_roles({})

# est-фид: отдельная привязка с ЗАПИСЯМИ ВСЕХ ТРЁХ ворот (1 @10°, 2 @121°,
# 3 @245°) — чтобы тест показать: найденные ворота сильнее привязки,
# потерянные (но настроенные) получают est, ненастроенные — нет.
E_SNAP = {
    1: {"angle_deg": 10.0, "dist_cm": 44.0},
    2: {"angle_deg": 121.0, "dist_cm": 90.0},
    3: {"angle_deg": 245.0, "dist_cm": None},
}
E_MAP_PATH = os.path.join(TMP, "binding3.json")
FM.save_map(E_MAP_PATH, FM.build_map_data(E_SNAP, "manual", 0.0))
U.FIELD_MAP = FM.load_map(E_MAP_PATH)
est_map = U._estimated_directions()
check("est map has all three bound gates", sorted(est_map) == [1, 2, 3])
radar.set_estimated(est_map)
local_recs = [
    {"slot": 0, "enabled": True, "found": True, "angle": 10, "cx": 10, "cy": 10,
     "area": 500.0, "w_px": 40, "r_px": 100.0, "radius": 20.0, "in_zone": True,
     "dist_cm": 44.0, "dist_reliable": True},
    {"slot": 1, "enabled": True, "found": False, "angle": -1, "cx": -1, "cy": -1,
     "area": 0.0, "w_px": 0, "r_px": float("inf"), "radius": 0.0,
     "in_zone": False, "dist_cm": None, "dist_reliable": False},
    {"slot": 2, "enabled": False, "found": False, "angle": -1, "cx": -1, "cy": -1,
     "area": 0.0, "w_px": 0, "r_px": float("inf"), "radius": 0.0,
     "in_zone": False, "dist_cm": None, "dist_reliable": False},
]
radar.set_local_results(local_recs)
gates = radar._gate_data()
check("est merge: LOST+configured gate 2 gets the bound direction",
      gates[1]["found"] is False and gates[1]["angle"] == 121.0
      and gates[1]["estimated"] is True, str(gates[1]))
check("est merge: bound dist_cm carried with the est direction",
      gates[1]["dist_cm"] == 90.0)
check("est merge: FOUND gate 1 keeps the live angle (est ignored)",
      gates[0]["angle"] == 10.0 and gates[0]["estimated"] is False)
check("est merge: unconfigured gate 3 gets no est",
      gates[2]["angle"] is None and gates[2]["estimated"] is False)
check("_target_line for est target ends with (est)",
      radar.set_target_gate(2) is None
      and radar._target_line(gates).endswith("(est)"),
      repr(radar._target_line(gates)))

# Пиксельный тест: приглушённая дуга на 121°, живая — полным цветом.
U.screen.fill((90, 90, 90))
radar.draw()
cx, cy, R, _f = radar._geo
cx += radar.rect.x
cy += radar.rect.y
Rg = R - RP.ARC_RADIUS_INSET


def dir_point(cxa, cya, radius, deg):
    a = math.radians(deg)
    return (cxa + radius * math.cos(a), cya - radius * math.sin(a))


dim2 = radar._dim_colors[1]
px, py = dir_point(cx, cy, Rg, 121.0)
c_at = U.screen.get_at((int(px), int(py)))
found_dim = any(
    U.screen.get_at((int(px) + dx, int(py) + dy))[:3] == dim2
    for dx in range(-4, 5) for dy in range(-4, 5)
)
check("est gate drawn as a DIMMED arc at the bound angle", found_dim)
px, py = dir_point(cx, cy, Rg, 10.0)
found_live = any(
    U.screen.get_at((int(px) + dx, int(py) + dy))[:3] == U.TRACK_COLORS[0]
    for dx in range(-4, 5) for dy in range(-4, 5)
)
check("found gate still drawn in FULL color", found_live)

# Пустой est-фид -> прежнее поведение (нет angle у потерянных ворот).
radar.set_estimated({})
gates = radar._gate_data()
check("empty est feed -> lost gate has no angle (old behavior)",
      gates[1]["angle"] is None and gates[1]["estimated"] is False)
check("set_estimated tolerates junk", (radar.set_estimated(None),
     radar.set_estimated({1: "junk", "x": {"angle_deg": 5}})) and
      radar.estimated == {})

# Idea 11 на радаре: опасный сектор 0 (0..45°, центр 22.5°) — красная дуга.
radar.set_edge_sectors([True] + [False] * 7)
U.screen.fill((90, 90, 90))
radar.draw()
px, py = dir_point(cx, cy, Rg, 22.5)
edge_px = any(
    U.screen.get_at((int(px) + dx, int(py) + dy))[:3] == RP.EDGE_ARC
    for dx in range(-5, 6) for dy in range(-5, 6)
)
check("dangerous sector 0 drawn with the EDGE arc color", edge_px)
px, py = dir_point(cx, cy, Rg, 202.5)  # центр сектора 5 — флага нет
no_edge = any(
    U.screen.get_at((int(px) + dx, int(py) + dy))[:3] == RP.EDGE_ARC
    for dx in range(-5, 6) for dy in range(-5, 6)
)
check("clean sector 5 has no EDGE arc", not no_edge)
radar.set_edge_sectors([])
U.screen.fill((90, 90, 90))
radar.draw()
px, py = dir_point(cx, cy, Rg, 22.5)
gone = any(
    U.screen.get_at((int(px) + dx, int(py) + dy))[:3] == RP.EDGE_ARC
    for dx in range(-5, 6) for dy in range(-5, 6)
)
check("clearing edge flags removes the arc (adviser off -> no visuals)", not gone)
radar.set_edge_sectors(None)
check("set_edge_sectors(None) tolerated", radar.edge_flags == [])
radar.set_visible(False)

# ============ F. статус-JSON: additive edge_sectors ============
print("=== F. status JSON additive key ===")
parsed = SP.parse_status_payload(json.dumps({
    "gates": {"1": {"enabled": True, "found": True, "angle": 52}},
    "fps": 25.9,
    "edge_sectors": [True, False, False, False, False, False, False, False],
    "edge_any": True,
}).encode())
check("parse_status_payload keeps edge_sectors in extra (schema unchanged)",
      parsed is not None and parsed["extra"].get("edge_any") is True
      and parsed["extra"]["edge_sectors"][0] is True)
check("gates/fps schema untouched by the extra key",
      parsed["gates"]["1"]["angle"] == 52 and parsed["fps"] == 25.9)
state = {"flags": [True, False], "n": 2}
check("status_extra shape", FB.status_extra(state)
      == {"edge_sectors": [True, False], "edge_any": True})
check("status_extra(None-ish state) safe", FB.status_extra(None)
      == {"edge_sectors": [], "edge_any": False})

# ============ G. цена ============
print("=== G. per-frame cost ===")
big = FM.load_map(TMP_MAP)
n = 20000
t0 = time.perf_counter()
for _ in range(n):
    U._estimated_directions()
est_ms = (time.perf_counter() - t0) / n * 1000
print(f"    estimated_directions: {est_ms:.4f} ms")
check("estimated_directions < 0.05 ms/frame", est_ms < 0.05)


def feed_once():
    radar.set_estimated(est_map)
    radar.set_edge_sectors([False] * 8)


t0 = time.perf_counter()
for _ in range(20000):
    feed_once()
feed_ms = (time.perf_counter() - t0) / 20000 * 1000
print(f"    radar est+edge feed: {feed_ms:.4f} ms/frame")
check("radar feed cost negligible (< 0.05 ms)", feed_ms < 0.05)

# ============ Интеграция: main_loop_frame кормит радар ============
print("=== H. main_loop_frame feed integration ===")
radar.set_visible(True)
radar.set_estimated({})
U.FIELD_MAP = FM.load_map(TMP_MAP)
frame = np.zeros((360, 640, 3), np.uint8)
frame[280:320, 40:120] = (255, 255, 0)
U.main_loop_frame(frame)
check("loop feeds estimated map to the radar",
      radar.estimated == {1: {"angle_deg": 52.0, "dist_cm": 148.0, "estimated": True}}
      or radar.estimated.get(1, {}).get("angle_deg") == 52.0)
check("loop feeds edge flags (detector on synthetic frame runs)",
      radar.edge_flags == [] or len(radar.edge_flags) == 8)
check("edge_state computed for a real frame (detector active)",
      U.edge_state is not None and U.edge_state.get("n") == 8)
check("shares present in edge_state (log/calibration data)",
      isinstance(U.edge_state.get("shares"), list)
      and len(U.edge_state["shares"]) == 8)
radar.set_visible(False)
U.FIELD_MAP = None  # вернуть состояние «без привязки» для других проверок

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
