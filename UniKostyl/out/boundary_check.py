# -*- coding: utf-8 -*-
"""out/boundary_check.py — camera-free verification of Idea 11 (field
boundary, camera part — adviser).

Covers (PROMPT_EN.md §5 Idea 11 / §6, no hardware):
  A. resolve_params: defaults, clamps, unknown keys ignored, degenerate
     ring (inner >= outer) pushed apart instead of dividing by emptiness.
  B. Synthetic field (green carpet + white line + dark "outside"):
     correct sector flags (angle convention 0°=right CCW, same as
     tracking/radar), aspect-preserving downscale keeps angles true on
     portrait 720x1280.
  C. No false positives: uniform green field, green + white marking line,
     green with mild shadow — ZERO flags over many frames.
  D. Hysteresis: danger confirmed on the 2nd consecutive frame (2-3 per
     PROMPT), one clean frame does NOT clear, release_frames do; config
     confirm_frames=3 works.
  E. Hue wrap: floor range through 0/179 still detects the carpet.
  F. Fusion placeholder (Idea 12 reverse channel): board priority over
     camera, None board = camera-only (today's mode).
  G. BoundaryDetector.process state shape; disabled params -> None.
  H. Real recorded frame (out/scene_red_folder.png — NOT a real field):
     runs without crash, shares finite in [0..1] (calibration on the real
     carpet is a separate, user-dependent step).
  I. Cost: per-frame process() negligible on PC (< 2 ms at 720x1280).

Run:  python out\\boundary_check.py   (from UniKostyl/, system Python)
"""
import math
import os
import shutil
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import field_boundary as FB

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.join(HERE, "boundary_check_tmp")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)

# "Ковёр": зелёный (RGB ~ (60, 160, 80) -> HSV H~75..80? — берём проверенное
# значение: RGB2HSV зелёного ковра обычно H 40..70, S высокий).
CARPET_RGB = (52, 148, 78)
OUTSIDE_RGB = (38, 38, 40)     # тёмный стол/борт за полем
WHITE_RGB = (240, 240, 240)    # белая разметка


def field_frame(w, h, outside="none", white_line=False, shadow=False):
    """Синтетический вид сверху: зелёный ковёр + опциональные зоны."""
    fr = np.full((h, w, 3), CARPET_RGB, np.uint8)
    if outside == "right":
        fr[:, int(w * 0.72):] = OUTSIDE_RGB
    elif outside == "top":
        fr[:int(h * 0.28), :] = OUTSIDE_RGB
    elif outside == "corner":
        fr[:int(h * 0.28), int(w * 0.72):] = OUTSIDE_RGB
    elif outside == "quad":
        # Весь верхне-правый квадрант «за полем» — крупные зоны, проверка
        # направления (сектора 0, 1, 7 = 315..135° через 0°).
        fr[: h // 2, w // 2:] = OUTSIDE_RGB
    if white_line:
        fr[:, int(w * 0.5) - 2:int(w * 0.5) + 2] = WHITE_RGB
    if shadow:
        # Мягкая тень: ковёр темнее, но V всё ещё выше floor_v_min.
        zone = fr[int(h * 0.35):int(h * 0.6), int(w * 0.3):int(w * 0.55)]
        fr[int(h * 0.35):int(h * 0.6), int(w * 0.3):int(w * 0.55)] = (
            zone * 0.6
        ).astype(np.uint8)
    return fr


P = FB.resolve_params({})

# ============ A. параметры ============
print("=== A. resolve_params ===")
check("defaults present", P["n_sectors"] == 8 and P["confirm_frames"] == 2
      and P["release_frames"] == 3 and P["enabled"] is True)
q = FB.resolve_params({"n_sectors": 4, "edge_share": 99, "inner_frac": 0.5,
                       "outer_frac": 0.4, "unknown_key": 1, "confirm_frames": "junk"})
check("clamps: n_sectors to 8..12, edge_share to 0.95, unknown key ignored",
      q["n_sectors"] == 8 and q["edge_share"] == 0.95
      and "unknown_key" not in q and q["confirm_frames"] == 2)
check("degenerate ring pushed apart (inner >= outer -> outer = inner+0.05)",
      q["outer_frac"] == min(0.90, q["inner_frac"] + 0.05))
q2 = FB.resolve_params("junk")
check("non-dict config -> defaults", q2 == P)
check("n_sectors high side clamped to 12",
      FB.resolve_params({"n_sectors": 20})["n_sectors"] == 12
      and FB.resolve_params({"n_sectors": 12})["n_sectors"] == 12)

# ============ B. геометрия секторов (синтетическое поле) ============
print("=== B. sector geometry (synthetic field) ===")
det = FB.BoundaryDetector(P)
check("default n_sectors == 8 (PROMPT: 8-12 sectors)", P["n_sectors"] == 8)

fr = field_frame(240, 240, outside="right")
raw, shares = FB.detect_sectors(fr, P)
check("right-side outside: sectors 0 and 7 flag (0 deg = right)",
      raw[0] is True and raw[7] is True,
      f"raw={[int(x) for x in raw]}")
check("left half clean (no outside there)",
      not raw[2] and not raw[3] and not raw[4] and not raw[5])
check("shares high on the outside side, ~0 on clean carpet",
      shares[0] > 0.5 and shares[7] > 0.5 and shares[3] < 0.1
      and all(0.0 <= s <= 1.0 for s in shares),
      f"shares={[round(s, 2) for s in shares]}")

fr_top = field_frame(240, 240, outside="top")
raw_top, _ = FB.detect_sectors(fr_top, P)
check("top outside: sectors 1 and 2 flag (90 deg = up in the frame)",
      raw_top[1] and raw_top[2] and not raw_top[5] and not raw_top[6])

fr_corner = field_frame(240, 240, outside="quad")
raw_corner, shares_corner = FB.detect_sectors(fr_corner, P)
check("upper-right quadrant outside: danger exactly in sectors 0 and 1 "
      "(top of image = +90°, CCW convention)",
      raw_corner[0] and raw_corner[1] and not any(raw_corner[2:]),
      f"shares={[round(s, 2) for s in shares_corner]}")

# Портретный кадр ноутбука (720x1280): пропорции сохраняются — углы честные.
fr_portrait = field_frame(720, 1280, outside="right")
raw_p, shares_p = FB.detect_sectors(fr_portrait, P)
check("portrait 720x1280: same sector semantics (aspect preserved)",
      raw_p[0] and raw_p[7] and not raw_p[3],
      f"shares={[round(s, 2) for s in shares_p]}")

# Белая разметка — ПОЛ: линия посреди ковра не даёт «границу».
fr_line = field_frame(240, 240, white_line=True)
raw_line, shares_line = FB.detect_sectors(fr_line, P)
check("white marking line counts as floor (no flags)", not any(raw_line),
      f"max share {max(shares_line):.3f}")

# ============ C. нет ложных срабатываний ============
print("=== C. no false positives ===")
det_c = FB.BoundaryDetector(P)
for i in range(10):
    st = det_c.process(field_frame(240, 240))
check("uniform green field: zero confirmed flags over 10 frames",
      st is not None and not any(st["flags"]) and not any(st["raw"]))
for i in range(6):
    st = det_c.process(field_frame(240, 240, white_line=True))
check("green + white line: zero confirmed flags", not any(st["flags"]))
for i in range(6):
    st = det_c.process(field_frame(240, 240, shadow=True))
check("mild shadow on carpet (V above floor_v_min): zero flags",
      not any(st["flags"]),
      f"shares={[round(s, 2) for s in st['shares']]}")

# ============ D. гистерезис ============
print("=== D. hysteresis (confirm 2-3 frames, release) ===")
det_d = FB.BoundaryDetector(P)
st = det_d.process(field_frame(240, 240, outside="right"))
check("frame 1 with outside: NOT confirmed yet (cand 1 < confirm 2)",
      st["raw"][0] and not st["flags"][0])
st = det_d.process(field_frame(240, 240, outside="right"))
check("frame 2 with outside: danger confirmed (2 consecutive frames)",
      st["flags"][0] and st["flags"][7])
st = det_d.process(field_frame(240, 240))
check("1 clean frame: flag held (clear 1 < release 3)", st["flags"][0])
st = det_d.process(field_frame(240, 240))
st = det_d.process(field_frame(240, 240))
st = det_d.process(field_frame(240, 240))
check("3 clean frames: danger released", not st["flags"][0] and not st["flags"][7])

det_e = FB.BoundaryDetector({"confirm_frames": 3})
st = det_e.process(field_frame(240, 240, outside="right"))
st = det_e.process(field_frame(240, 240, outside="right"))
check("confirm_frames=3: still not confirmed after 2 frames",
      not st["flags"][0])
st = det_e.process(field_frame(240, 240, outside="right"))
check("confirm_frames=3: confirmed on the 3rd frame (PROMPT 2-3 range)",
      st["flags"][0])

# ============ E. hue wrap ============
print("=== E. hue wrap (floor range through 0/179) ===")
PW = FB.resolve_params({"floor_h_lo": 170, "floor_h_hi": 20})
import colorsys  # stdlib: точная RGB->HSV математика для sanity-проверки
_h, _s, _v = colorsys.rgb_to_hsv(*(c / 255.0 for c in CARPET_RGB))
carpet_h = int(round(_h * 180)) % 180  # OpenCV H = hue/2 в градусах
check("carpet hue known (test sanity)", 0 <= carpet_h <= 179, f"H={carpet_h}")
check("carpet hue OUTSIDE the wrap range (test sanity: 20 < H < 170)",
      20 < carpet_h < 170, f"H={carpet_h}")
# Красный «ковёр» (H~0, внутри обёртки 170..20) — пол; без обёртки H=0
# выпадал бы из диапазона.
red_lo = np.full((240, 240, 3), (170, 40, 40), np.uint8)  # RGB красного
raw_w, shares_w = FB.detect_sectors(red_lo, PW)
check("wrap range: red carpet is floor (no flags on uniform red)",
      not any(raw_w), f"max share {max(shares_w):.3f}")
raw_wr, shares_wr = FB.detect_sectors(field_frame(240, 240), PW)
check("wrap range: green carpet is NOT in the red wrap range -> flagged",
      all(raw_wr))

# ============ F. fusion (заглушка Идеи 12) ============
print("=== F. fusion placeholder (board priority) ===")
cam_state = {"flags": [True, False, False], "n": 3}
check("board None (today): camera-only",
      FB.fuse(cam_state, None) == [True, False, False])
check("board OR camera: board wins conflicts",
      FB.fuse(cam_state, [False, True, False]) == [True, True, False])
check("camera-only False stays False with clean board",
      FB.fuse(cam_state, [False, False, False]) == [True, False, False])
check("board longer than camera padded",
      FB.fuse({"flags": [True], "n": 1}, [False, True]) == [True, True])
check("fuse(None camera, board) = board",
      FB.fuse(None, [True, False]) == [True, False])

det_f = FB.BoundaryDetector(P)
st = det_f.process(field_frame(240, 240), board_flags=[True] + [False] * 7)
check("process(): board input flows through (priority over camera)",
      st["flags"][0] is True and st["board_used"] is True
      and st["camera_flags"][0] is False)
st = det_f.process(field_frame(240, 240))
check("process() without board: camera-only mode (board_used False)",
      st["board_used"] is False)

# ============ G. состояние детектора ============
print("=== G. detector state ===")
check("None frame -> None", FB.BoundaryDetector(P).process(None) is None)
det_off = FB.BoundaryDetector({"enabled": False})
check("disabled -> None (adviser off = zero cost)",
      det_off.process(field_frame(240, 240)) is None)
st = det.process(field_frame(240, 240, outside="right"))
check("state keys: n/raw/camera_flags/flags/shares/any/ts",
      st["n"] == 8 and len(st["raw"]) == 8 and len(st["camera_flags"]) == 8
      and len(st["flags"]) == 8 and len(st["shares"]) == 8
      and st["any"] == any(st["flags"]) and st["ts"] > 0)
st2 = det.process(field_frame(240, 240, outside="right"))
check("hysteresis lives across process() calls",
      st2["flags"][0] and st2["flags"][7])

# ============ H. реальный записанный кадр (НЕ поле) ============
print("=== H. real recorded frame smoke ===")
scene_path = os.path.join(HERE, "scene_red_folder.png")
if os.path.exists(scene_path):
    scene_bgr = cv2.imread(scene_path)
    scene_rgb = cv2.cvtColor(scene_bgr, cv2.COLOR_BGR2RGB)
    det_h = FB.BoundaryDetector(P)
    t0 = time.perf_counter()
    st = det_h.process(scene_rgb)
    ms = (time.perf_counter() - t0) * 1000
    check("scene frame processed without crash (8 sectors)",
          st is not None and st["n"] == 8)
    check("shares finite and within [0..1]",
          all(math.isfinite(s) and 0.0 <= s <= 1.0 for s in st["shares"]),
          f"shares={[round(s, 2) for s in st['shares']]}")
    print(f"    scene_red_folder.png: shares={[round(s, 2) for s in st['shares']]}")
    print(f"    NOTE: scene is a desk, not the real field — carpet thresholds")
    print(f"    don't apply; this only proves robustness (UNVERIFIED ON HW)")
else:
    print("[SKIP] out/scene_red_folder.png not found — real-frame smoke skipped")

# ============ I. цена ============
print("=== I. per-frame cost ===")
for size, budget in (((720, 1280), 1.5), ((640, 360), 1.0)):
    fr = field_frame(size[0], size[1], outside="right")
    d = FB.BoundaryDetector(P)
    d.process(fr)  # прогрев (кэш масок)
    n = 300
    best = float("inf")
    for _ in range(3):  # min из 3 серий: гасит GC/планировщик при параллельной
        t0 = time.perf_counter()  # компиляции других агентов (как в status_check)
        for _ in range(n):
            d.process(fr)
        best = min(best, (time.perf_counter() - t0) / n * 1000)
    ms = best
    print(f"    process {size[0]}x{size[1]}: {ms:.3f} ms/frame (min of 3x{n})")
    check(f"cost {size[0]}x{size[1]} < {budget} ms/frame (adviser negligible)",
          ms < budget)

# Кэш масок пересобирается при смене размера (без утечки «чужой» геометрии).
r1, _ = FB.detect_sectors(field_frame(240, 240, outside="right"), P)
r2, _ = FB.detect_sectors(field_frame(480, 480, outside="right"), P)
r3, _ = FB.detect_sectors(field_frame(240, 240, outside="right"), P)
check("sector mask cache survives size alternation",
      r1 == r3 and r1[0] and r2[0])

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
