#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""acceptance_check.py — приёмочный тест «красная папка + зелёное полотенце».

Живая ELP-камера (автодетект полного FOV) + реальный пайплайн Pi-стороны
(GateTracker: inRange -> self-маска Идеи 8 -> морфология -> кластеризация
Идеи 5 -> гистерезис -> дистанция Идеи 6 -> статус-JSON Идеи 2/8/9) и
персистентность Идеи 3 (saved_gates round-trip = «рестарт без повторного
выделения»).

Сценарий:
  1. На живом кадре находим красную папку и зелёное полотенце (HSV-маски),
     семплируем их цвета -> G1 (красная) / G2 (зелёная).
  2. 45 кадров трекинга: G1 = ровно один стабильный объект, угол/площадь
     не скачут; зелёное полотенце не попадает в G1.
  3. G2 найден, его центр ВНЕ self-маски (зелёная плата Pi в центре кадра
     не перехватывает захват — Идея 8).
  4. saved_gates round-trip: запись через GateStore -> «рестарт» (свежий
     трекер + load_gates) -> ворота найдены снова без повторного выделения.
  5. build_gate_status: JSON содержит gates/found/angle/area/dist_cm.
"""
import os
import sys

import cv2 as cv
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # UniKostyl/

import rpi_elp_cam as R  # noqa: E402  (GateTracker, build_gate_status)
import self_mask as SM  # noqa: E402
import gate_cluster as GC  # noqa: E402
import gate_store as GS  # noqa: E402
from fisheye import find_full_fov_camera  # noqa: E402

CHECKS = []


def check(name, ok):
    CHECKS.append((name, bool(ok)))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")


def largest_cc(mask, min_area):
    n, labels, stats, _ = cv.connectedComponentsWithStats(mask, 8)
    best, best_area = None, min_area
    for i in range(1, n):
        a = stats[i, cv.CC_STAT_AREA]
        if a >= best_area:
            best_area = a
            best = i
    if best is None:
        return None
    x, y, w, h = (stats[best, cv.CC_STAT_LEFT], stats[best, cv.CC_STAT_TOP],
                  stats[best, cv.CC_STAT_WIDTH], stats[best, cv.CC_STAT_HEIGHT])
    ys, xs = np.nonzero(labels == best)
    return {"area": int(best_area), "cx": float(xs.mean()), "cy": float(ys.mean()),
            "pixels": None}


def sample_pixels(frame_bgr, mask, exclude_circle=None, min_s=0):
    """Медианный BGR цвет по маске; exclude_circle=(cx,cy,r) вырезает зону.

    min_s — фильтр насыщенности: глюки лампы (низкая S) и подмешанные
    чужие пиксели уводят СРЕДНЕЕ, медиана по насыщенному ядру предмета
    даёт цвет, который реалистично получает SAVE."""
    m = mask.copy()
    if exclude_circle is not None:
        cx, cy, r = exclude_circle
        yy, xx = np.mgrid[0:m.shape[0], 0:m.shape[1]]
        m[(xx - cx) ** 2 + (yy - cy) ** 2 <= r * r] = 0
    if min_s > 0:
        hsv = cv.cvtColor(frame_bgr, cv.COLOR_BGR2HSV)
        m = m & (hsv[:, :, 1] >= min_s)
    ys, xs = np.nonzero(m)
    if len(ys) == 0:
        return None, None
    med_bgr = np.median(frame_bgr[ys, xs], axis=0)
    centroid = (float(xs.mean()), float(ys.mean()))
    return med_bgr, centroid


def main():
    # 1. Камера: автодетект полного FOV.
    idx = find_full_fov_camera()
    check("ELP auto-detected (full FOV 1920x1080)", idx is not None)
    if idx is None:
        idx = 1
    cap = cv.VideoCapture(idx, cv.CAP_MSMF)
    cap.set(cv.CAP_PROP_FOURCC, cv.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv.CAP_PROP_FRAME_HEIGHT, 1080)
    check("camera opened", cap.isOpened())

    # Рабочий размер = пайплайн Pi (640x360, программный даунскейл 1080p).
    W, H = 640, 360
    frame = None
    for _ in range(8):
        ok, f = cap.read()
        if ok:
            frame = cv.resize(f, (W, H), interpolation=cv.INTER_AREA)
    check("frames arriving", frame is not None)
    if frame is None:
        return 1
    cv.imwrite(os.path.join(HERE, "acceptance_scene.png"), frame)

    hsv = cv.cvtColor(frame, cv.COLOR_BGR2HSV)

    # 2. Красная папка: красный = два hue-диапазона с обёрткой через 0.
    red = cv.inRange(hsv, (0, 80, 50), (10, 255, 255)) | \
        cv.inRange(hsv, (170, 80, 50), (179, 255, 255))
    red = cv.morphologyEx(red, cv.MORPH_OPEN, np.ones((5, 5), np.uint8))
    red_cc = largest_cc(red, 1500)
    check("red folder visible in frame", red_cc is not None)
    # Зелёное полотенце: вне self-маски (плата Pi в центре не считается).
    green = cv.inRange(hsv, (35, 80, 40), (85, 255, 255))
    green = cv.morphologyEx(green, cv.MORPH_OPEN, np.ones((5, 5), np.uint8))
    excl = (W // 2, H // 2, int(0.22 * min(W, H)))
    g_mean, g_centroid = sample_pixels(frame, green, exclude_circle=excl, min_s=100)
    check("green towel visible outside the body zone", g_mean is not None)
    if red_cc is None or g_mean is None:
        print("Сцена не содержит ожидаемых предметов — тест остановлен честно.")
        return 1
    red_mean, red_centroid = sample_pixels(frame, red, min_s=120)
    red_rgb = tuple(int(v) for v in cv.cvtColor(
        np.uint8([[red_mean]]), cv.COLOR_BGR2RGB)[0][0])
    green_rgb = tuple(int(v) for v in cv.cvtColor(
        np.uint8([[g_mean]]), cv.COLOR_BGR2RGB)[0][0])
    # Тестовый допуск: узкий (±18 по hue), как реальный SAVE по воротам.
    # Дефолтные ±40 накрыли бы оранжевый стол и розовую засветку —
    # тот самый сценарий «захватил майку». Узкий диапазон = калибровка.
    test_tolerance = (18, 100, 100)
    print(f"    red folder: rgb={red_rgb} at ({red_centroid[0]:.0f},{red_centroid[1]:.0f})")
    print(f"    green towel: rgb={green_rgb} at ({g_centroid[0]:.0f},{g_centroid[1]:.0f})")

    # 3. Реальный трекер: 2 ворот, self-маска (Идея 8), дефолты Идеи 5.
    area_min = max(8.0, 100 * W * H / (1920.0 * 1080.0))
    tracker = R.GateTracker(W, H, test_tolerance, area_min,
                            (1920.0 * 1080.0) / (W * H), 2, dist_enabled=True)
    sm_params = SM.resolve_params(W, H)
    tracker.set_self_mask(sm_params)
    tracker.update_gate_from_rgb(0, red_rgb)    # G1 = красная папка
    tracker.update_gate_from_rgb(1, green_rgb)  # G2 = зелёное полотенце

    def run_frames(n):
        out = {0: [], 1: []}
        infos = {}
        for _ in range(n):
            ok, f = cap.read()
            if not ok:
                continue
            small = cv.resize(f, (W, H), interpolation=cv.INTER_AREA)
            cv.cvtColor(small, cv.COLOR_BGR2HSV, dst=tracker.hsv)
            for gid in (0, 1):
                info = tracker.process(gid)
                infos[gid] = info
                if info is not None:
                    out[gid].append(info)
        return out, infos

    # 4. 45 кадров живого трекинга.
    res, last_infos = run_frames(45)
    check("G1 (red folder) found most frames", len(res[0]) >= 36)  # >=80%
    check("G2 (green towel) found on enough frames", len(res[1]) >= 27)  # >=60%
    if res[0]:
        angles = [i["angle"] for i in res[0]]
        areas = [i["area"] for i in res[0]]
        cxs = [i["cx"] for i in res[0]]
        cys = [i["cy"] for i in res[0]]
        ang_std = float(np.std([a if a <= 180 else 360 - a for a in angles]))
        area_cv = float(np.std(areas) / (np.mean(areas) + 1e-9))
        check(f"G1 single stable object (angle std {ang_std:.1f} deg < 3)",
              ang_std < 3.0)
        check(f"G1 area stable (CV {area_cv:.2f} < 0.3)",
              area_cv < 0.3)
        # Центр G1 у красной папки, не у полотенца/платы.
        d_to_red = float(np.hypot(np.mean(cxs) - red_centroid[0],
                                  np.mean(cys) - red_centroid[1]))
        check(f"G1 centroid at the red folder ({d_to_red:.0f} px < 60)",
              d_to_red < 60.0)
    # Self-маска: ни один центр трекинга не внутри зоны корпуса.
    excl_r = sm_params["r"]
    inside = [i for gid in (0, 1) for i in res[gid]
              if (i["cx"] - sm_params["cx"]) ** 2 +
              (i["cy"] - sm_params["cy"]) ** 2 <= excl_r * excl_r]
    check("no tracked centroid inside the body zone (Idea 8)", not inside)
    with open(os.path.join(HERE, "acceptance_debug.txt"), "w") as dbg:
        for gid in (0, 1):
            dbg.write(f"G{gid+1}: {len(res[gid])} frames\n")
            for i in res[gid]:
                dbg.write(f"  cx={i['cx']:.0f} cy={i['cy']:.0f} ang={i['angle']} "
                          f"area={i['area']:.0f} r={i.get('radius', 0):.0f}\n")
    # Дистанция (Идея 6, аналитический фолбэк) присутствует и вменяема.
    dists = [i.get("dist_cm") for i in res[0] if i.get("dist_cm") is not None]
    check("dist_cm present and sane (0 < d <= 500)",
          bool(dists) and all(0.0 < d <= 500.0 for d in dists))

    # 5. Статус-JSON (Идеи 2/8/9): gates/found/angle/area/dist_cm.
    status = R.build_gate_status(tracker, last_infos, 25.9)
    check("status JSON has gates 1..2 with found flag",
          set(status["gates"].keys()) == {"1", "2"}
          and all("found" in g for g in status["gates"].values()))

    # 6. saved_gates round-trip = «рестарт Pi без повторного выделения».
    store_path = os.path.join(HERE, "acceptance_saved_gates.json")
    store = GS.GateStore(store_path, min_interval=0.0)
    store.set_rgb(1, red_rgb, enabled=True)
    store.set_rgb(2, green_rgb, enabled=True)
    store.flush_pending()
    reloaded = GS.GateStore(store_path, min_interval=0.0)
    restored = reloaded.restore_rgb()
    check("saved_gates round-trip: 2 gates restored",
          sorted(g for g, _ in restored) == [1, 2])
    tracker2 = R.GateTracker(W, H, test_tolerance, area_min,
                             (1920.0 * 1080.0) / (W * H), 2, dist_enabled=True)
    tracker2.set_self_mask(sm_params)
    for gid, rgb in restored:
        tracker2.update_gate_from_rgb(gid - 1, rgb)
    # (отдельный цикл для второго трекера)
    out2 = {0: [], 1: []}
    for _ in range(20):
        ok, f = cap.read()
        if not ok:
            continue
        small = cv.resize(f, (W, H), interpolation=cv.INTER_AREA)
        cv.cvtColor(small, cv.COLOR_BGR2HSV, dst=tracker2.hsv)
        for gid in (0, 1):
            info = tracker2.process(gid)
            if info is not None:
                out2[gid].append(info)
    check("after 'restart' both gates tracked again (no re-selection)",
          len(out2[0]) >= 15 and len(out2[1]) >= 11)

    # 7. Итог.
    cv.imwrite(os.path.join(HERE, "acceptance_last_frame.png"),
               cv.resize(frame, (960, 540)))
    fails = [n for n, ok in CHECKS if not ok]
    for name, ok in CHECKS:
        print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    print(f"TOTAL: {len(CHECKS) - len(fails)} passed, {len(fails)} failed")
    cap.release()
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
