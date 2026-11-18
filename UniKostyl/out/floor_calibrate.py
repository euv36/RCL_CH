#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""floor_calibrate.py — калибровка порогов «пол/не-пол» для Идеи 11.

Проблема: детектор границы поля настроен на зелёный ковёр + белую
разметку регламента. Пока робот стоит на столе (или на другой подложке),
детектор честно помечает все сектора как «край» — это НЕ баг, это смена
подложки. Скрипт сэмплирует цвет «пола» из кадра и пишет пороги в секцию
boundary файла field_map.json (атомарно), после чего детектор надо
проверить на той же сцене.

Использование:
  python out/floor_calibrate.py --live                     # кадр с камеры
  python out/floor_calibrate.py --image frame.png          # сохранённый кадр
  python out/floor_calibrate.py --image f.png --sample 320,180
      # явная точка сэмплинга (иначе — кольцевая полоса вокруг центра,
      # где по регламенту должен быть ковёр)
Скрипт печатает пороги, процент кадра, классифицированного как «пол», и
флаги секторов по той же сцене (проверка тут же, на месте).
"""
import argparse
import json
import os
import sys

import cv2 as cv
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # UniKostyl/

import field_map as FM  # noqa: E402
import field_boundary as FB  # noqa: E402


def grab_live():
    from fisheye import find_full_fov_camera
    idx = find_full_fov_camera()
    if idx is None:
        idx = 1
    cap = cv.VideoCapture(idx, cv.CAP_MSMF)
    cap.set(cv.CAP_PROP_FOURCC, cv.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv.CAP_PROP_FRAME_HEIGHT, 1080)
    frame = None
    for _ in range(8):
        ok, f = cap.read()
        if ok:
            frame = f
    cap.release()
    return frame


def sample_floor_hsv(hsv, frame_shape, sample):
    """Сэмплинг HSV «пола»: либо явная точка (окно 41x41), либо кольцевая
    полоса 0.26..0.42 от min-размера вокруг центра (рабочая полоса
    детектора). Возвращает (h_lo, h_hi, s_min, v_min) по перцентилям."""
    h, w = frame_shape[:2]
    mask = np.zeros((h, w), np.uint8)
    if sample is not None:
        x, y = sample
        cv.rectangle(mask, (max(0, x - 20), max(0, y - 20)),
                     (min(w - 1, x + 20), min(h - 1, y + 20)), 255, -1)
    else:
        yy, xx = np.mgrid[0:h, 0:w]
        r = np.sqrt((xx - w / 2.0) ** 2 + (yy - h / 2.0) ** 2)
        r_min, r_max = 0.26 * min(w, h), 0.42 * min(w, h)
        mask[(r >= r_min) & (r <= r_max)] = 255
    pix = hsv[mask > 0]
    if len(pix) < 500:
        return None
    hues = pix[:, 0].astype(np.int32)
    h_med = int(np.median(hues))
    # Перцентильный диапазон hue с учётом потенциального перехода через 0:
    # раскладываем отклонение от медианы в круговом порядке.
    d = (hues - h_med + 90) % 180 - 90  # -90..90
    lo_d, hi_d = np.percentile(d, [5, 95])
    h_lo, h_hi = (h_med + int(lo_d)) % 180, (h_med + int(hi_d)) % 180
    s_min = int(np.percentile(pix[:, 1], 10)) - 15
    v_min = int(np.percentile(pix[:, 2], 10)) - 15
    return (max(0, h_lo), h_hi % 180, max(0, min(255, s_min)),
            max(0, min(255, v_min)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", help="кадр поля (PNG/JPG)")
    ap.add_argument("--live", action="store_true", help="кадр с камеры")
    ap.add_argument("--sample", help="точка сэмплинга пола x,y (пиксели)")
    ap.add_argument("--map", help="путь к field_map.json (по умолчанию штатный)")
    args = ap.parse_args()

    if args.live:
        frame = grab_live()
    elif args.image:
        frame = cv.imread(args.image)
    else:
        ap.error("нужен --live или --image")
    if frame is None:
        print("Кадр не получен")
        return 1
    hsv = cv.cvtColor(frame, cv.COLOR_BGR2HSV)

    sample = None
    if args.sample:
        x, y = (int(v) for v in args.sample.split(","))
        sample = (x, y)
    got = sample_floor_hsv(hsv, frame.shape, sample)
    if got is None:
        print("Слишком мало пикселей в зоне сэмплинга — укажите --sample x,y")
        return 1
    h_lo, h_hi, s_min, v_min = got
    span = (h_hi - h_lo) % 180
    print(f"Пол сэмплирован: hue {h_lo}..{h_hi} (span {span}), "
          f"S>={s_min}, V>={v_min}")
    if span > 60:
        print("ВНИМАНИЕ: диапазон hue шире 60 — подложка неоднородна; "
              "проверьте, что сэмплируете именно ковёр (см. превью).")

    # Атомарно вписать в field_map.json (секция boundary, остальное цело).
    map_path = args.map or FM.default_path()
    data = FM.load_map(map_path)
    if data is None:
        data = {"version": 1, "gates": {}}
    bnd = dict(data.get("boundary") or {})
    bnd.update({"floor_h_lo": int(h_lo), "floor_h_hi": int(h_hi),
                "floor_s_min": int(s_min), "floor_v_min": int(v_min),
                "enabled": bool(bnd.get("enabled", True)),
                "n_sectors": int(bnd.get("n_sectors", 8)),
                "inner_frac": float(bnd.get("inner_frac", 0.24)),
                "outer_frac": float(bnd.get("outer_frac", 0.46)),
                "white_s_max": int(bnd.get("white_s_max", 70)),
                "white_v_min": int(bnd.get("white_v_min", 185)),
                "edge_share": float(bnd.get("edge_share", 0.55)),
                "confirm_frames": int(bnd.get("confirm_frames", 2)),
                "release_frames": int(bnd.get("release_frames", 3)),
                "work_size": int(bnd.get("work_size", 128))})
    data["boundary"] = bnd
    ok = FM.save_map(map_path, data)
    print(f"field_map.json {'записан' if ok else 'НЕ записан (ошибка)'}: "
          f"floor hue {h_lo}..{h_hi}, S>={s_min}, V>={v_min}")

    # Тут же проверка: детектор с новыми порогами на той же сцене.
    # ВАЖНО: BoundaryDetector.process принимает RGB (не BGR) и возвращает dict.
    det = FB.BoundaryDetector(bnd)
    st = det.process(cv.cvtColor(frame, cv.COLOR_BGR2RGB))
    flags = list(st["flags"]) if st else []
    n = len(flags)
    print(f"Сектора ({n}): " +
          " ".join(f"{k}:{'EDGE' if fl else 'ok'}" for k, fl in enumerate(flags)))
    prev_dir = os.path.join(HERE, "floor_calib_preview.png")
    prev = frame.copy()
    step = 2.0 * np.pi / max(n, 1)
    for k, fl in enumerate(flags):
        if not fl:
            continue
        a = k * step + step / 2.0
        R = 0.44 * min(prev.shape[1], prev.shape[0])
        p1 = (int(prev.shape[1] / 2 + 0.24 * R * np.cos(a)),
              int(prev.shape[0] / 2 + 0.24 * R * np.sin(a)))
        p2 = (int(prev.shape[1] / 2 + 0.46 * R * np.cos(a)),
              int(prev.shape[0] / 2 + 0.46 * R * np.sin(a)))
        cv.line(prev, p1, p2, (0, 0, 255), 3)
    cv.imwrite(prev_dir, prev)
    print(f"Превью: {prev_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
