# -*- coding: utf-8 -*-
"""out/select_check.py — camera-free verification of the gate SELECT flow.

Баг (2026-11-18, «выделение ворот не работает»): после выделения красной
папки мышью на img_src порог считался НЕ по выделению. Две переставленные
оси: (1) пересчёт экранных координат брал shape[0] (высоту кадра) для X и
shape[1] (ширину) для Y; (2) threshold_from_area/threshold_diff срезали
pixels[rect.left:rect.right, rect.top:rect.bottom] — поперёк осей numpy
(H, W). Итог: порог брался с «транспонированного» участка кадра в другом
месте — диапазон покрывал почти весь кадр, КРОМЕ цвета ворот, и маска
выглядела инвертированной (всё жёлтое, ворота чёрные).

Covers:
  A. selection_rect(): экранный rect -> пиксели кадра для виджетов разных
     размеров/пропорций (виджет растягивает кадр без сохранения пропорций),
     клампы у границ виджета, пустые/невалидные выделения -> None.
  B. threshold_from_area() на плоском участке: порог ТОЧНО равен цвету
     участка (min==max) — регрессионная ловушка осей; threshold_filter()
     выделяет участок и не выделяет фон.
  C. Неровный участок (градиент «папки с тенью»): все пиксели участка внутри
     бокса min/max, фон снаружи.
  D. threshold_diff(): без noise.dll == threshold_from_area; с dll —
     валидный 6-элементный результат (smoke).
  E. Сквозной прогон compute_track() с порогом из выделения: ворота найдены,
     центр/угол/площадь совпадают с участком, combined покрашен цветом слота
     (то самое «выделение жёлтым» из бага).

Run:  python out\\select_check.py   (from UniKostyl/, system Python)
"""
import os
import sys

import numpy as np
import pygame

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U  # noqa: E402  (создаёт pygame-окно — ок под dummy)

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


# ============ Сцена: портретный кадр 720x1280 (как в UI) ============
W, H = 720, 1280
# Плоская «красная папка» (слот G1) внизу слева — асимметрично: транспонированное
# сэмплирование старого кода попадало бы совсем в другое место кадра.
P = (x0, y0, x1, y1) = (64, 896, 160, 992)  # 96x96 px
PATCH_RGB = (200, 30, 30)
BG_A_RGB = (235, 235, 235)  # светло-серый фон
BG_B_RGB = (40, 40, 45)     # тёмный фон

frame = np.empty((H, W, 3), dtype=np.uint8)
frame[: H // 2] = BG_A_RGB
frame[H // 2:] = BG_B_RGB
frame[y0:y1, x0:x1] = PATCH_RGB
pixels = U.rgb2lab(frame)
patch_ref = pixels[P[1] + 1, P[0] + 1].copy()  # эталонный LAB участка

# ============ A. selection_rect: экранный rect -> пиксели ============
print("=== A. selection_rect mapping ===")
# Виджеты с точными кратностями (rw/W, rh/H ∈ {0.25, 0.5, 0.75}), чтобы
# экранные координаты участка вычислялись без округлений.
for rw, rh in ((360, 640), (540, 640), (180, 320)):
    img_r = pygame.Rect(30, 40, rw, rh)
    sx_f, sy_f = rw / W, rh / H  # прямое отображение кадр -> экран
    temp_area = pygame.Rect(
        int(img_r.left + P[0] * sx_f), int(img_r.top + P[1] * sy_f),
        int((P[2] - P[0]) * sx_f), int((P[3] - P[1]) * sy_f),
    )
    got = U.selection_rect(temp_area, img_r, pixels)
    check(
        f"selection_rect widget {rw}x{rh}: точный rect участка",
        got is not None
        and (got.left, got.top, got.right, got.bottom) == P,
        f"got={got}",
    )

# Клампы: выделение выходит за края виджета -> границы кадра.
img_r = pygame.Rect(30, 40, 360, 640)
temp_area = pygame.Rect(img_r.left, img_r.top, 360, 640)  # весь виджет
got = U.selection_rect(temp_area, img_r, pixels)
check(
    "selection_rect: весь виджет -> весь кадр",
    got is not None
    and (got.left, got.top, got.right, got.bottom) == (0, 0, W, H),
    f"got={got}",
)

# Пустые/невалидные выделения -> None.
check(
    "selection_rect: нулевая ширина -> None",
    U.selection_rect(pygame.Rect(100, 100, 0, 50), img_r, pixels) is None,
)
check(
    "selection_rect: вне виджета -> None",
    U.selection_rect(pygame.Rect(0, 0, 10, 10), pygame.Rect(30, 40, 360, 640), pixels)
    is None,
)
check(
    "selection_rect: виджет 0 px -> None",
    U.selection_rect(pygame.Rect(0, 0, 5, 5), pygame.Rect(0, 0, 0, 0), pixels) is None,
)
check(
    "selection_rect: pixels не 3D -> None",
    U.selection_rect(pygame.Rect(0, 0, 5, 5), img_r, np.zeros((4, 4))) is None,
)

# ============ B. Плоский участок: порог == цвет участка ============
print("=== B. threshold_from_area: оси и плоский участок ===")
rect = pygame.Rect(P[0], P[1], P[2] - P[0], P[3] - P[1])
thr = U.threshold_from_area(rect, pixels)
# Плоский участок: порог = [L,L,A,A,B,B] с min==max по каждому каналу.
l0, a0, b0 = map(int, patch_ref)
check(
    "порог плоского участка == его LAB (min==max) — ловушка осей",
    list(map(int, thr)) == [l0, l0, a0, a0, b0, b0],
    f"thr={list(map(int, thr))}, участок=[{l0},{a0},{b0}]",
)
check(
    "порог не дефолтный «весь кадр»",
    list(map(int, thr)) != [0, 100, -128, 127, -128, 127],
)

mask = U.threshold_filter(thr, pixels)
mask_gray = mask[:, :, 0].copy()
# Фон считаем БЕЗ участка (участок лежит в тёмной половине кадра).
mask_gray[P[1]:P[3], P[0]:P[2]] = 0
patch_on = float((mask[P[1]:P[3], P[0]:P[2], 0] > 0).mean())
bg_a_on = float((mask_gray[: H // 2] > 0).mean())
bg_b_on = float((mask_gray[H // 2:] > 0).mean())
check("маска: участок выделен (100%)", patch_on == 1.0, f"patch_on={patch_on:.3f}")
check("маска: светлый фон не выделен", bg_a_on < 0.01, f"bg_a_on={bg_a_on:.4f}")
check("маска: тёмный фон не выделен", bg_b_on < 0.01, f"bg_b_on={bg_b_on:.4f}")

# ============ C. Неровный участок: градиент внутри бокса ============
print("=== C. градиентный участок ===")
frame2 = frame.copy()
g = np.linspace(0.0, 1.0, P[2] - P[0])
for j, gx in enumerate(g):
    col = (int(200 - 60 * gx), int(30 + 25 * gx), int(30 + 10 * gx))
    frame2[P[1]:P[3], P[0] + j] = col
pixels2 = U.rgb2lab(frame2)
thr2 = U.threshold_from_area(rect, pixels2)
mask2 = U.threshold_filter(thr2, pixels2)[:, :, 0].copy()
mask2[P[1]:P[3], P[0]:P[2]] = 0
check(
    "градиентный участок: все пиксели внутри бокса min/max",
    float((U.threshold_filter(thr2, pixels2)[P[1]:P[3], P[0]:P[2], 0] > 0).mean())
    == 1.0,
)
check(
    "градиентный участок: фон по-прежнему снаружи",
    float((mask2[: H // 2] > 0).mean()) < 0.01
    and float((mask2[H // 2:] > 0).mean()) < 0.01,
)

# ============ D. threshold_diff ============
print("=== D. threshold_diff ===")
td = U.threshold_diff([0, 100, -128, 127, -128, 127], rect, pixels)
if U.noise_lib is None:
    check(
        "threshold_diff без noise.dll == threshold_from_area",
        list(map(int, td)) == list(map(int, thr)),
        f"td={list(map(int, td))}",
    )
else:
    ok = isinstance(td, (list, tuple)) and len(td) == 6 and all(
        isinstance(v, int) for v in td
    )
    check("threshold_diff с noise.dll: 6 int, без падения", ok, f"td={td}")

# ============ E. Сквозной прогон compute_track ============
print("=== E. compute_track с порогом из выделения ===")
U.thresholds[0] = list(map(int, thr))
U.thresholds[1] = list(U.THRESHOLD_DEFAULT)
U.thresholds[2] = list(U.THRESHOLD_DEFAULT)
combined, track_results, target_state, current_angle = U.compute_track(pixels)
rec = track_results[0]
area_expect = float((P[2] - P[0]) * (P[3] - P[1]))
# Центроид пикселей 64..159/896..991 = 111.5/943.5, rec хранит int() — усечение.
cx_expect = (P[0] + P[2] - 1) // 2
cy_expect = (P[1] + P[3] - 1) // 2
dx, dy = cx_expect - W // 2, H // 2 - cy_expect
ang_expect = int(np.degrees(np.arctan2(dy, dx)) % 360)
check("compute_track: ворота найдены", rec["found"] is True)
check(
    f"compute_track: центр == центру участка ({cx_expect},{cy_expect})",
    rec["cx"] == cx_expect and rec["cy"] == cy_expect,
    f"got=({rec['cx']},{rec['cy']})",
)
check(
    f"compute_track: площадь == площади участка ({area_expect:.0f})",
    abs(rec["area"] - area_expect) < 1.0,
    f"got={rec['area']:.0f}",
)
check(
    f"compute_track: угол в квадранте участка ({ang_expect}±20°)",
    abs(((rec["angle"] - ang_expect + 180) % 360) - 180) <= 20,
    f"got={rec['angle']}",
)
check(
    "compute_track: combined покрашен жёлтым слота G1 в центре участка",
    tuple(int(v) for v in combined[cy_expect, cx_expect]) == U.TRACK_COLORS[0],
    f"got={tuple(int(v) for v in combined[cy_expect, cx_expect])}",
)
check(
    "compute_track: фон combined не покрашен (маска не инвертирована)",
    float(
        (
            (combined[:, :, 0] == 255)
            & (combined[:, :, 1] == 255)
            & (combined[:, :, 2] == 0)
        ).mean()
    )
    < 0.30,
)


if FAILURES:
    print(f"\nRESULT: FAILED — {len(FAILURES)}: {FAILURES}")
    sys.exit(1)
print("\nRESULT: all checks passed")
