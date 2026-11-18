# -*- coding: utf-8 -*-
"""out/radar_check.py — camera-free verification of Idea 7 (radar panel).

Covers (PROMPT_EN.md §5 Idea 7 / §6, no hardware):
  A. Registration in unikostyl: radar_panel + btn_radar widgets, default
     hidden, toggle_radar() flips visibility + button state hint.
  B. Data units: _gate_data (pi/local sources, role passthrough, held,
     dist_cm/dist_reliable, angle sanitation), _target_line ("r = N px →
     M cm" formats incl. dash until Idea 6), _marker_frac (dist > r_px >
     fixed), set_body_zone (Idea 8 SELF_MASK mapping), set_roles (unset ==
     absence), sync_from_status_panel (single parse, identity of pi_status).
  C. Pixel tests on the rendered radar (SDL dummy): gate arcs at their
     angles (0/120/240 + 358° wrap), role badge arcs present/absent per the
     unset invariant, target marker circle + center line at the selected
     gate, held arcs dimmed, stale Pi status (> 2 s) falls back to local
     results like status_panel.
  D. Integration: main_loop_frame feeds local results + target; layout
     rect (right column, under status panel, no overlap) + resize growth;
     pi_status identity with the status panel widget.
  E. Costs: visible draw < 0.5 ms, hidden draw ~0, per-frame feed ~0.

Run:  python out\\radar_check.py   (from UniKostyl/, system Python)
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

import radar_panel as RP
import status_panel as SP

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


TMP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "radar_check_tmp")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)

BG_GREY = (90, 90, 90)


def color_at(surface, x, y):
    c = surface.get_at((int(x), int(y)))
    return (c.r, c.g, c.b)


def color_near(surface, x, y, color, radius=5):
    """True, если точный цвет найден в квадрате (2*radius+1) вокруг точки.

    pygame.draw.* не антиалиасят — середина дуги/маркера совпадает точно.
    """
    w, h = surface.get_size()
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            px, py = int(x) + dx, int(y) + dy
            if 0 <= px < w and 0 <= py < h and color_at(surface, px, py) == tuple(color):
                return True
    return False


def dir_point(cx, cy, radius, deg):
    """Точка на направлении deg (0°=вправо, CCW — конвенция трекинга)."""
    a = math.radians(deg)
    return (cx + radius * math.cos(a), cy - radius * math.sin(a))


# ============ A. регистрация в unikostyl + переключение ============
print("=== A. unikostyl registration + toggle ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U

radar = U.widgets.get("radar_panel")
btn = U.widgets.get("btn_radar")
check("radar_panel widget registered", radar is not None)
check("btn_radar registered in top row", btn is not None)
check("radar hidden by default (button opens it)", radar is not None and radar.visible is False)
check("button label RADAR", btn is not None and btn.label.text == "RADAR")
check("radar is display-only (mousedown passes through)",
      radar.process_mousedown(type("E", (), {"pos": (0, 0)})()) is False)

# Запрос пользователя: кнопка RADAR открывает ОТДЕЛЬНОЕ ресайз-окно (cv2
# WINDOW_NORMAL), а не панель в правой колонке. Панель остаётся скрытой.
U.toggle_radar()
check("toggle_radar opens the separate window", radar._win_open is True)
check("button shows ON state hint (green label)",
      btn.label.color == (185, 255, 185))
U.toggle_radar()
check("toggle_radar closes the window again", radar._win_open is False)
check("button back to white label", btn.label.color == (255, 255, 255))
# Крестик в title bar: open=True, но окно больше не visible -> pump сбрасывает open.
U.toggle_radar()
_old_prop = RP._cv2.getWindowProperty
try:
    RP._cv2.getWindowProperty = lambda *_a, **_k: 0.0
    radar.pump_window(do_waitkey=True)
    check("X-close (VISIBLE<0.5) resets _win_open", radar._win_open is False)
finally:
    RP._cv2.getWindowProperty = _old_prop
    if radar._win_open:
        radar.close_window()

# ============ B. юниты данных ============
print("=== B. data units ===")

# --- B1. set_roles: unset-инвариант (мусор -> бейджа нет) ---
radar.set_roles({1: "ours", 2: "opponent", 3: "junk", "1": "opponent"})
check("set_roles keeps only set roles (int ids, junk dropped)",
      radar.roles == {1: "opponent", 2: "opponent"})
radar.set_roles({})
check("set_roles({}) clears roles", radar.roles == {})

# --- B2. sync_from_status_panel: единый разбор статуса Pi ---
sp = U.widgets.get("status_panel")
check("status_panel widget exists (sync source)", sp is not None)
parsed = SP.parse_status_payload(json.dumps({
    "gates": {
        "1": {"enabled": True, "found": True, "angle": 10.0, "area": 300,
              "dist_cm": 148.0, "dist_reliable": True, "role": "ours"},
        "2": {"enabled": True, "found": True, "angle": 120, "area": 250},
        "3": {"enabled": False, "found": False},
    },
    "fps": 25.9,
}).encode())
sp.on_pi_status(parsed)
sp.on_video_source("pi", "10.0.0.5")
sp.set_roles({2: "opponent"})
radar.sync_from_status_panel(sp)
check("sync copies pi_status BY REFERENCE (no duplicate parsing)",
      radar.pi_status is sp.pi_status)
check("sync copies source_kind", radar.source_kind == "pi")
check("sync copies roles", radar.roles == {2: "opponent"})
check("_use_pi True for a fresh status", radar._use_pi() is True)

# --- B3. _gate_data: поля статуса пробрасываются, role только когда задан ---
gates = radar._gate_data()
check("3 gate records", len(gates) == 3)
g1, g2, g3 = gates
check("gate 1: found, angle, dist_cm reliable, role ours",
      g1["found"] and g1["angle"] == 10.0 and g1["dist_cm"] == 148.0
      and g1["dist_ok"] and g1["role"] == "ours")
check("gate 2: found, NO role key -> role None (unset == absence)",
      g2["found"] and g2["role"] is None)
check("gate 3: disabled -> not configured, no angle",
      not g3["configured"] and not g3["found"] and g3["angle"] is None)

# --- B4. angle sanitation: 362 -> 2 (без пересчёта атан2, только % 360) ---
parsed2 = SP.parse_status_payload(json.dumps({
    "gates": {"1": {"enabled": True, "found": True, "angle": 362}},
}).encode())
radar.on_pi_status(parsed2)
check("angle 362 normalized to 2 (identity for valid values)",
      radar._gate_data()[0]["angle"] == 2.0)
radar.on_pi_status(parsed)  # откат

# --- B5. _target_line: форматы подписи цели ---
radar.set_target_gate(1)
check("pi found + reliable dist: 'G1 10° r = ? px -> 148 cm'",
      radar._target_line(gates) == "G1  10\u00b0  r = ? px  -> 148 cm",
      repr(radar._target_line(gates)))
parsed_nodist = SP.parse_status_payload(json.dumps({
    "gates": {"1": {"enabled": True, "found": True, "angle": 52, "area": 300}},
}).encode())
radar.on_pi_status(parsed_nodist)
check("pi found, no dist (до Идеи 6): 'G1 52° r = ? px -> —'",
      radar._target_line(radar._gate_data())
      == "G1  52\u00b0  r = ? px  -> \u2014")
parsed_bad = SP.parse_status_payload(json.dumps({
    "gates": {"1": {"enabled": True, "found": True, "angle": 52,
                    "dist_cm": 90.0, "dist_reliable": False}},
}).encode())
radar.on_pi_status(parsed_bad)
check("pi dist_reliable false -> dash (семантика статус-панели)",
      radar._target_line(radar._gate_data()).endswith("-> \u2014"))
radar.on_pi_status(parsed)

# локальный режим
local_recs = [
    {"slot": 0, "enabled": True, "found": True, "angle": 52, "cx": 10, "cy": 10,
     "area": 500.0, "w_px": 40, "r_px": 263.0, "radius": 20.0,
     "in_zone": True, "dist_cm": 48.0},
    {"slot": 1, "enabled": True, "found": False, "angle": -1, "cx": -1,
     "cy": -1, "area": 0.0, "w_px": 0, "r_px": float("inf"), "radius": 0.0,
     "in_zone": False, "dist_cm": None},
    {"slot": 2, "enabled": False, "found": False, "angle": -1, "cx": -1,
     "cy": -1, "area": 0.0, "w_px": 0, "r_px": float("inf"), "radius": 0.0,
     "in_zone": False, "dist_cm": None},
]
radar.on_video_source("local", "0")
radar.set_local_results(local_recs)
radar.set_target_gate(1)
gates_local = radar._gate_data()
check("local gate 1: found + r_px + dist_cm, role None (roles cleared)",
      gates_local[0]["found"] and gates_local[0]["r_px"] == 263.0
      and gates_local[0]["dist_cm"] == 48.0 and gates_local[0]["role"] is None)
check("local gate 2: configured but lost -> no angle",
      gates_local[1]["configured"] and not gates_local[1]["found"]
      and gates_local[1]["angle"] is None)
check("local gate 3: not configured",
      not gates_local[2]["configured"])
check("local target line with dist: 'G1 52° r = 263 px -> 48 cm'",
      radar._target_line(gates_local)
      == "G1  52\u00b0  r = 263 px  -> 48 cm")
local_recs[0]["dist_cm"] = None
check("local target line without dist (до Идеи 6): 'r = 263 px -> —'",
      radar._target_line(radar._gate_data())
      == "G1  52\u00b0  r = 263 px  -> \u2014")
check("empty local results -> 'G1: n/a'",
      radar._target_line([]) == "G1: n/a")
undrawn_recs = [dict(r) for r in local_recs]
for _r in undrawn_recs:
    _r["enabled"] = False
    _r["found"] = False
    _r["angle"] = -1
radar.set_local_results(undrawn_recs)
unconfigured = radar._gate_data()
check("all unconfigured local gates -> zero angles (nothing to draw)",
      all(not g["configured"] and g["angle"] is None for g in unconfigured))
radar.set_local_results(local_recs)  # restore
local_recs[0]["dist_cm"] = 48.0

# Записи без enabled не считаются настроенными: stale/saved Pi status не должен
# показывать ворота, пока пользователь не выбрал цвет слота.
parsed_missing_enabled = SP.parse_status_payload(json.dumps({
    "gates": {
        "1": {"found": True, "angle": 10.0},
        "2": {"found": False},
        "3": {"found": True, "angle": 120.0, "role": "ours"},
    },
}).encode())
radar.on_video_source("pi", "10.0.0.5")
radar.on_pi_status(parsed_missing_enabled)
missing_pi = radar._gate_data()
check("pi gates missing enabled -> no gates displayed",
      all(not g["configured"] and g["angle"] is None for g in missing_pi),
      repr(missing_pi))
radar.on_pi_status(parsed)  # restore fresh status
missing_local_recs = [dict(r) for r in local_recs]
for _r in missing_local_recs:
    _r.pop("enabled", None)
radar.on_video_source("local", "0")
radar.set_local_results(missing_local_recs)
missing_local = radar._gate_data()
check("local gates missing enabled -> no gates displayed",
      all(not g["configured"] and g["angle"] is None for g in missing_local),
      repr(missing_local))
radar.set_local_results(local_recs)  # restore

# --- B6. _marker_frac: dist_cm > r_px > фиксированная позиция ---
tgt = {"dist_cm": 148.0, "dist_ok": True, "r_px": None}
check("marker frac from dist_cm (Idea 6): 148/250",
      abs(radar._marker_frac(tgt, 0.0) - 148.0 / RP.RADAR_MAX_CM) < 1e-9)
tgt2 = {"dist_cm": 148.0, "dist_ok": False, "r_px": 263.0}
check("unreliable dist ignored -> r_px mapping",
      abs(radar._marker_frac(tgt2, 360.0) - 263.0 / 360.0) < 1e-9)
tgt3 = {"dist_cm": None, "dist_ok": True, "r_px": None}
check("no data -> default marker frac",
      radar._marker_frac(tgt3, 0.0) == RP.DEFAULT_MARKER_FRAC)
check("marker frac clamped high/low",
      radar._marker_frac({"dist_cm": 5000.0, "dist_ok": True, "r_px": None}, 0.0) == 0.92
      and radar._marker_frac({"dist_cm": 1.0, "dist_ok": True, "r_px": None}, 0.0) == 0.30)

# --- B7. set_body_zone: Idea 8 SELF_MASK -> доля радиуса диска ---
radar.set_body_zone(True, 0.22, 0.0)
check("body zone r=0.22 -> 0.44 of disc radius", abs(radar.body_frac - 0.44) < 1e-9)
radar.set_body_zone(False, 0.22, 0.0)
check("disabled self-mask -> default body frac",
      radar.body_frac == RP.DEFAULT_BODY_FRAC)
radar.set_body_zone(True, 0.30, 0.45)
check("ring outer radius mapped + clamped",
      abs(radar.body_frac - 0.6) < 1e-9 and abs(radar.body_frac2 - 0.9) < 1e-9)
radar.set_body_zone(True, "junk")
check("junk fractions -> default (never raises)",
      radar.body_frac == RP.DEFAULT_BODY_FRAC)

# --- B8. role local: бейджи из set_roles ---
radar.set_roles({1: "ours"})
check("local role picked up from set_roles",
      radar._gate_data()[0]["role"] == "ours")
radar.set_roles({})

# ============ C. пиксельные тесты отрисовки ============
print("=== C. pixel tests (SDL dummy) ===")
radar.set_visible(True)
radar.rect = pygame.Rect(200, 80, 340, 340)
radar.set_body_zone(True, 0.22, 0.0)  # как SELF_MASK по умолчанию
# локальные результаты не должны мешать pi-режиму
radar.set_local_results([])
radar.on_video_source("pi", "10.0.0.5")
# Оба роли в СТАТУСЕ (Idea 9: pi-режим читает "role" из JSON, не из set_roles):
# gates 1@10° (ours), 2@120° (opponent), 3 disabled (без ключа role).
parsed_c = SP.parse_status_payload(json.dumps({
    "gates": {
        "1": {"enabled": True, "found": True, "angle": 10.0, "area": 300,
              "dist_cm": 148.0, "dist_reliable": True, "role": "ours"},
        "2": {"enabled": True, "found": True, "angle": 120, "area": 250,
              "role": "opponent"},
        "3": {"enabled": False, "found": False},
    },
    "fps": 25.9,
}).encode())
radar.on_pi_status(parsed_c)
radar.set_target_gate(2)

U.screen.fill(BG_GREY)
radar.draw()
cx, cy, R, _footer = radar._geo
cx += radar.rect.x
cy += radar.rect.y
Rg = R - RP.ARC_RADIUS_INSET
Rr = R - RP.ROLE_RADIUS_INSET
check("disc geometry sane", R > 60, f"R={R}")

YELLOW = U.TRACK_COLORS[0]
BLUE = U.TRACK_COLORS[1]
ORANGE = U.TRACK_COLORS[2]

# --- C1. дуги ворот на своих углах (все активные одновременно) ---
for gid, ang, col in ((1, 10.0, YELLOW), (2, 120.0, BLUE)):
    px, py = dir_point(cx, cy, Rg, ang)
    check(f"gate {gid} arc drawn at {ang:.0f} deg",
          color_near(U.screen, px, py, col), f"want {col} near ({px:.0f},{py:.0f})")
check("disabled gate 3 has no arc",
      not color_near(U.screen, *dir_point(cx, cy, Rg, 240.0), ORANGE, 4))

# --- C2. бейджи роли: green/red только у заданных (unset == отсутствие) ---
px, py = dir_point(cx, cy, Rr, 10.0)
check("role badge OURS (green) at gate 1",
      color_near(U.screen, px, py, RP.ROLE_OURS, 4))
px, py = dir_point(cx, cy, Rr, 120.0)
check("role badge OPPONENT (red) at gate 2 (role from status JSON)",
      color_near(U.screen, px, py, RP.ROLE_OPPONENT, 4))
has_badge3 = (color_near(U.screen, *dir_point(cx, cy, Rr, 240.0), RP.ROLE_OURS, 6)
              or color_near(U.screen, *dir_point(cx, cy, Rr, 240.0), RP.ROLE_OPPONENT, 6))
check("no role badge for unset gate 3 (absence of key == no badge)",
      not has_badge3)

# --- C3. маркер цели: круг + линия от центра у выбранного gate 2 ---
mr = radar._marker_frac(g2, 0.0) * R
marker_r = RP.marker_radius(R)
px, py = dir_point(cx, cy, mr + marker_r - 1, 120.0)
check("target marker circle outline at gate 2 direction",
      color_near(U.screen, px, py, RP.MARKER_TXT, 3))
px, py = dir_point(cx, cy, mr, 120.0)
check("marker circle around the point",
      color_near(U.screen, px, py, RP.MARKER_TXT, marker_r // 2 + 2))
px, py = dir_point(cx, cy, mr * 0.5, 120.0)
check("line from center to marker",
      color_near(U.screen, px, py, RP.MARKER_TXT, 2))
# у неназначенной цели (gate 1) маркера быть не должно
px, py = dir_point(cx, cy, RP.DEFAULT_MARKER_FRAC * R, 10.0)
check("no marker at non-target gate 1",
      not color_near(U.screen, px, py, RP.MARKER_TXT, 3))

shot = os.path.join(TMP, "radar_pi.png")
pygame.image.save(U.screen.subsurface(pygame.Rect(radar.rect)), shot)
print(f"    saved {shot}")

# --- C4. переход 359->1: дуга у 358° рисуется (обход нуля в draw.arc) ---
parsed_wrap = SP.parse_status_payload(json.dumps({
    "gates": {"1": {"enabled": True, "found": True, "angle": 358},
              "2": {"enabled": True, "found": True, "angle": 120}},
}).encode())
radar.on_pi_status(parsed_wrap)
U.screen.fill(BG_GREY)
radar.draw()
px, py = dir_point(cx, cy, Rg, 358.0)
check("gate arc near 0° wrap (358°) drawn",
      color_near(U.screen, px, py, YELLOW, 5))

# --- C5. stale-статус: старше 2 с -> возврат к локальным результатам ---
# Источник остаётся Pi (как в живом cv_kostyl), локальные результаты идут
# тем же фидом параллельно — радар выбирает по свежести статуса.
radar.on_video_source("pi", "10.0.0.5")
radar.set_local_results(local_recs)  # gate 1 @ 52°, r_px 263
radar.set_roles({1: "ours"})
radar.on_pi_status(parsed)  # pi: gate 1 @ 10°, свежий
radar.set_target_gate(1)
check("_use_pi True while fresh", radar._use_pi() is True)
U.screen.fill(BG_GREY)
radar.draw()
px, py = dir_point(cx, cy, Rg, 10.0)
check("fresh pi status wins (arc at pi angle 10°)",
      color_near(U.screen, px, py, YELLOW, 5))
px, py = dir_point(cx, cy, Rg, 52.0)
check("local angle not drawn while pi fresh",
      not color_near(U.screen, px, py, YELLOW, 5))

radar.pi_status["ts"] = time.monotonic() - (SP.PI_STATUS_STALE_S + 0.5)
check("_use_pi False when older than PI_STATUS_STALE_S", radar._use_pi() is False)
U.screen.fill(BG_GREY)
radar.draw()
px, py = dir_point(cx, cy, Rg, 52.0)
check("stale pi -> local tracking wins (arc at local angle 52°)",
      color_near(U.screen, px, py, YELLOW, 5))
px, py = dir_point(cx, cy, Rg, 10.0)
check("stale pi angle not drawn", not color_near(U.screen, px, py, YELLOW, 5))
check("stale pi -> local dist_cm in footer",
      radar._target_line(radar._gate_data()) == "G1  52\u00b0  r = 263 px  -> 48 cm")
shot = os.path.join(TMP, "radar_stale_local.png")
pygame.image.save(U.screen.subsurface(pygame.Rect(radar.rect)), shot)
print(f"    saved {shot}")

# --- C6. held (гистерезис Идеи 5): дуга приглушённым цветом ---
radar.on_pi_status(None)
held_recs = [dict(local_recs[0]), dict(local_recs[1]), dict(local_recs[2])]
held_recs[0]["held"] = True
radar.set_local_results(held_recs)
U.screen.fill(BG_GREY)
radar.draw()
dim = radar._dim_colors[0]
px, py = dir_point(cx, cy, Rg, 52.0)
check("held arc drawn dimmed", color_near(U.screen, px, py, dim, 5))
check("held arc NOT drawn in full color",
      not color_near(U.screen, px, py, YELLOW, 5))

# --- C7. маркер по r_px (локальный режим, frame size задан) ---
held_recs[0]["held"] = False
held_recs[0]["dist_cm"] = None  # без Идеи 6: радиус маркера от r_px
radar.set_frame_size(720, 1280)  # рабочий кадр ноутбука (портрет)
U.screen.fill(BG_GREY)
radar.draw()
mr = 263.0 / (720 / 2.0)
px, py = dir_point(cx, cy, mr * R, 52.0)
check("local marker positioned by r_px / (min(w,h)/2)",
      color_near(U.screen, px, py, RP.MARKER_TXT, marker_r // 2 + 2))
held_recs[0]["dist_cm"] = 48.0  # Idea 6 появляется -> маркер по dist_cm
U.screen.fill(BG_GREY)
radar.draw()
px, py = dir_point(cx, cy, 0.30 * R, 52.0)  # 48/250 -> clamp 0.30
check("local marker switches to dist_cm scale automatically",
      color_near(U.screen, px, py, RP.MARKER_TXT, marker_r // 2 + 2))
check("footer shows cm once dist_cm present",
      radar._target_line(radar._gate_data()).endswith("-> 48 cm"))

# ============ D. интеграция с unikostyl ============
print("=== D. integration (main_loop_frame feed + layout) ===")
U.update_all_widgets()  # вернуть штатную раскладку 1280x720
check("default radar rect: right column under status panel",
      radar.rect == pygame.Rect(1002, 561, 250, 99), str(radar.rect))
sp_rect = sp.rect
check("radar below status panel, no overlap",
      radar.rect.y >= sp_rect.y + sp_rect.h)
check("radar inside screen", radar.rect.right <= 1280 and radar.rect.bottom <= 720)
check("btn_radar in compact top row (after CAM SET)",
      btn.rect == pygame.Rect(575, 14, 78, 36), str(btn.rect))

frame = np.zeros((360, 640, 3), np.uint8)
frame[280:320, 40:120] = (255, 255, 0)  # жёлтый кусок для локального трекинга
U.main_loop_frame(frame)
check("main_loop_frame feeds local results to radar",
      len(radar.local_results) == 3 and radar.local_results[0]["slot"] == 0)
check("main_loop_frame feeds target gate (TARGET G3 default)",
      radar.target_gate == 3)
check("frame size fed from tracking frame",
      radar._frame_min == 360)
check("body zone fed from Idea 8 SELF_MASK (r=0.22 -> 0.44)",
      abs(radar.body_frac - 0.44) < 1e-9)
check("radar.pi_status is status_panel.pi_status after loop",
      radar.pi_status is sp.pi_status)
# Возврат к локальному источнику, чтобы не зависеть от состояния панели.
U.set_video_source("local", "0")
U.set_pi_status(None)
U.main_loop_frame(frame)
check("loop with pi_status=None: radar falls back to local",
      radar._use_pi() is False)

# Resize: окно больше — радар вырастает (до 260), кнопка остаётся в ряду.
U.screen_w, U.screen_h = 1600, 900
U.update_all_widgets()
check("radar grows on bigger window (h=260)",
      radar.rect.height == 260 and radar.rect.width == 250, str(radar.rect))
U.screen_w, U.screen_h = 1280, 720
U.update_all_widgets()

# ============ E. цена ============
print("=== E. per-frame cost ===")
# подготовка «боевого» состояния: pi свежий, 3 ворот, роли, маркер, метки
radar.set_visible(True)
radar.on_video_source("pi", "10.0.0.5")
full = SP.parse_status_payload(json.dumps({
    "gates": {
        "1": {"enabled": True, "found": True, "angle": 10.0, "area": 300,
              "dist_cm": 148.0, "dist_reliable": True, "role": "ours"},
        "2": {"enabled": True, "found": True, "angle": 120.0, "area": 250,
              "role": "opponent"},
        "3": {"enabled": True, "found": True, "angle": 245.0, "area": 180},
    },
    "fps": 25.9,
}).encode())
radar.on_pi_status(full)
radar.set_target_gate(1)
U.screen.fill(BG_GREY)
radar.draw()  # прогрев кэша текста/фона
n = 500
t0 = time.perf_counter()
for _ in range(n):
    radar.draw()
draw_ms = (time.perf_counter() - t0) / n * 1000
print(f"    radar.draw visible: {draw_ms:.3f} ms")
check("visible draw < 0.5 ms/frame", draw_ms < 0.5)

radar.set_visible(False)
t0 = time.perf_counter()
for _ in range(5000):
    radar.draw()
hidden_ms = (time.perf_counter() - t0) / 5000 * 1000
print(f"    radar.draw hidden: {hidden_ms:.5f} ms")
check("hidden draw ~ zero (< 0.02 ms)", hidden_ms < 0.02)


def feed_once():
    radar.set_local_results(local_recs)
    radar.set_target_gate(1)
    radar.set_body_zone(True, 0.22, 0.0)
    radar.set_frame_size(720, 1280)
    radar.sync_from_status_panel(sp)


feed_once()
t0 = time.perf_counter()
for _ in range(20000):
    feed_once()
feed_ms = (time.perf_counter() - t0) / 20000 * 1000
print(f"    radar feed (5 setters, hidden): {feed_ms:.4f} ms/frame")
check("feed cost negligible (< 0.05 ms/frame)", feed_ms < 0.05)

# финальный скриншот включённого радара в штатной раскладке
radar.set_visible(True)
U.screen.fill((200, 200, 200))
radar.draw()
shot = os.path.join(TMP, "radar_final.png")
pygame.image.save(U.screen.subsurface(pygame.Rect(radar.rect)), shot)
print(f"    saved {shot}")

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
