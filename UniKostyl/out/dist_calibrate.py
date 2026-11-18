#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""out/dist_calibrate.py — Idea 6: калибровка дистанции до ворот по размеру.

Собирает пары (известная дистанция в см -> измеренный радиус ворот в px),
подбирает константы модели d = a/(b - r) - c методом наименьших квадратов
(линеаризация + полировка Гаусса-Ньютона, см. UniKostyl/distance.py) и
АТОМАРНО пишет distance_calib.json. Печатает отчёт о качестве фита
(невязки по каждой точке).

Режимы:
  * Точки из сохранённых кадров:
      python out\\dist_calibrate.py --image кадр.png --dist 120 --gate 1
      (цвет из saved_gates.json, радиус меряется тем же конвейером, что
       и в рантайме: inRange -> self-маска -> OPEN/CLOSE -> gate_cluster;
       кадр приводится к --track-size, по умолчанию 640x360 — рабочее
       разрешение Pi).
  * Живая камера:
      python out\\dist_calibrate.py --live --dist 120 --gate 1 --preview
      Пробел — записать точку, [ / ] — изменить дистанцию на 10 см,
      f — перефитить, q — выход. Робот ставится на известную дистанцию,
      --dist обновляется клавишами (или перезапуском с новым значением).
  * Вручную (без камеры вообще):
      python out\\dist_calibrate.py --add 120,263.5 --add 90,300.2
      r вводится в px кадра --frame-size (по умолчанию 640x360; при
      другом размере точки пересчитываются к масштабу сессии).
  * Отчёт/правка:
      python out\\dist_calibrate.py --list     # модель + невязки
      python out\\dist_calibrate.py --remove 2 # убрать точку №2 (с 1)
      python out\\dist_calibrate.py --clear    # убрать все точки
      python out\\dist_calibrate.py --fit      # перефитить и сохранить

Точки накапливаются в out/dist_calibrate_points.json (сессия); как только
их >= 3 и фит проходит валидацию — модель с точками атомарно пишется в
distance_calib.json (рядом с UniKostyl/distance.py, --out переопределяет).
Рекомендуется 4-6 точек по всему рабочему диапазону дистанций.

Запуск из каталога UniKostyl (или репозитория): python out\\dist_calibrate.py
"""

import argparse
import json
import math
import os
import sys
import time

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import distance as D  # noqa: E402  (модуль Idea 6, рядом с rpi_elp_cam.py)
import gate_cluster   # noqa: E402

try:
    import gate_store
except ImportError:
    gate_store = None

try:
    import self_mask as SM
except ImportError:
    SM = None

try:
    import fisheye
except ImportError:
    fisheye = None

SESSION_NAME = "dist_calibrate_points.json"
SESSION_VERSION = 1


# ===================== сессия точек =====================
def session_path():
    return os.path.join(HERE, SESSION_NAME)


def load_session(path=None):
    """Точки сессии + масштаб кадра. Битый файл -> пустая сессия."""
    path = path or session_path()
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return {"frame_w": 0, "frame_h": 0, "points": []}
    except Exception as exc:
        print(f"[DIST-CAL] сессия {os.path.basename(path)} повреждена ({exc}) — начинаю пустую")
        return {"frame_w": 0, "frame_h": 0, "points": []}
    pts = []
    if isinstance(data.get("points"), list):
        for p in data["points"]:
            try:
                d, r = float(p[0]), float(p[1])
            except (TypeError, ValueError, IndexError):
                continue
            if math.isfinite(d) and math.isfinite(r) and d > 0 and r > 0:
                pts.append([d, r])
    try:
        fw, fh = int(data.get("frame_w", 0)), int(data.get("frame_h", 0))
    except (TypeError, ValueError):
        fw = fh = 0
    return {"frame_w": fw if fw > 0 else 0, "frame_h": fh if fh > 0 else 0,
            "points": pts}


def save_session(sess, path=None):
    path = path or session_path()
    data = {
        "version": SESSION_VERSION,
        "frame_w": sess["frame_w"],
        "frame_h": sess["frame_h"],
        "points": [[float(d), float(r)] for d, r in sess["points"]],
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    tmp = "%s.%d.tmp" % (path, os.getpid())
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        return True
    except Exception as exc:
        print(f"[DIST-CAL] не удалось записать сессию {path}: {exc}")
        try:
            os.remove(tmp)
        except OSError:
            pass
        return False


def points_from_calib(out_path):
    """Точки из уже существующего distance_calib.json (добавление к нему)."""
    try:
        with open(out_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return None
    if not isinstance(data, dict) or not isinstance(data.get("points"), list):
        return None
    try:
        fw, fh = int(data.get("frame_w", 0)), int(data.get("frame_h", 0))
    except (TypeError, ValueError):
        fw = fh = 0
    pts = []
    for p in data["points"]:
        try:
            d, r = float(p[0]), float(p[1])
        except (TypeError, ValueError, IndexError):
            continue
        if math.isfinite(d) and math.isfinite(r) and d > 0 and r > 0:
            pts.append([d, r])
    return {"frame_w": fw if fw > 0 else 0, "frame_h": fh if fh > 0 else 0,
            "points": pts}


def fit_and_save(sess, out_path, quiet=False, session_file=None):
    """Фит + атомарная запись distance_calib.json, если точек достаточно.

    Точек < 3 / фит отклонён: сессия сохраняется, distance_calib.json
    не трогается. Возвращает (model_dict | None, report | None).
    """
    pts = [(d, r) for d, r in sess["points"]]
    model_d, report = D.fit_model(pts)
    if model_d is None:
        save_session(sess, session_file)
        if not quiet:
            print(f"[DIST-CAL] фит не выполнен: {report}")
            print(f"[DIST-CAL] точки сохранены в сессию ({len(pts)} шт), "
                  f"{os.path.basename(out_path)} не тронут")
        return None, None
    data = {
        "version": D.VERSION,
        "model": D.MODEL_NAME,
        "a": model_d["a"],
        "b": model_d["b"],
        "c": model_d["c"],
        "points": [[float(d), float(r)] for d, r in model_d["points"]],
        "frame_w": sess["frame_w"],
        "frame_h": sess["frame_h"],
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    # Контрольная валидация через DistModel (та же проверка, что при загрузке).
    m = D.model_from_dict(data)
    if m is None or not m.valid:
        save_session(sess, session_file)
        if not quiet:
            print("[DIST-CAL] модель отклонена валидацией: "
                  + (m.error if m else "битый dict"))
        return None, None
    if D.save_model_dict(out_path, data):
        if not quiet:
            print(f"[DIST-CAL] модель записана в {out_path} (атомарно)")
    save_session(sess, session_file)
    return data, report


def scale_r_to_session(r, declared_w, declared_h, sess):
    """Пересчёт r, введённого для кадра --frame-size, к масштабу сессии.

    Радиус линеен по кадру (один FOV), мера — min(ширина, высота).
    """
    if not (declared_w > 0 and declared_h > 0):
        return r
    if sess["frame_w"] > 0 and sess["frame_h"] > 0:
        base = min(sess["frame_w"], sess["frame_h"])
    else:
        base = min(declared_w, declared_h)
    src = min(declared_w, declared_h)
    if src <= 0 or base <= 0 or src == base:
        return r
    return r * base / float(src)


# ===================== измерение r (тот же конвейер, что в рантайме) =====================
def hsv_ranges_from_rgb(r, g, b, tol):
    """H-диапазоны как в rpi_elp_cam.update_gate_from_rgb (обёртка 0/179)."""
    hsv = cv2.cvtColor(np.uint8([[[r, g, b]]]), cv2.COLOR_RGB2HSV)[0][0]
    h, s, v = (int(x) for x in hsv)
    tol_h, tol_s, tol_v = tol
    lo_h, hi_h = h - tol_h, h + tol_h
    if lo_h < 0:
        ranges = [(0, hi_h), (lo_h + 180, 179)]
    elif hi_h > 179:
        ranges = [(lo_h, 179), (0, hi_h - 180)]
    else:
        ranges = [(lo_h, hi_h)]
    ranges = [(max(0, int(a)), min(179, int(bb))) for a, bb in ranges if bb >= a]
    return ranges, (max(0, s - tol_s), min(255, s + tol_s),
                    max(0, v - tol_v), min(255, v + tol_v))


def gate_color(args):
    """RGB для измерения: --rgb > --gate N (из saved_gates.json) > жёлтый."""
    if args.rgb:
        return tuple(int(v) for v in args.rgb), "CLI --rgb"
    if args.gate and gate_store is not None:
        gates = gate_store.load_gates()
        rec = gates.get(str(args.gate))
        if isinstance(rec, dict) and rec.get("rgb"):
            rgb = tuple(int(c) for c in rec["rgb"])
            return rgb, f"saved_gates.json G{args.gate}"
        print(f"[DIST-CAL] в saved_gates.json нет цвета G{args.gate} — "
              f"использую жёлтый по умолчанию")
    return (255, 255, 0), "умолчание (жёлтый G1)"


def measure_radius(frame_bgr, rgb, tol, params, keep=None):
    """Радиус слитого объекта того же цвета — тем же конвейером, что рантайм.

    frame_bgr уже приведён к рабочему размеру. Возвращает (radius | None,
    mask для превью). Голубой/жёлтый/оранжевый цвет даёт ту же меру r,
    что GateTracker.process в rpi_elp_cam.py (radius = описанная окружность
    бокса слитого кластера, Idea 5).
    """
    frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    hsv = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2HSV)
    ranges, (s_lo, s_hi, v_lo, v_hi) = hsv_ranges_from_rgb(rgb[0], rgb[1], rgb[2], tol)
    mask = None
    tmp = np.empty(hsv.shape[:2], dtype=np.uint8)
    for lo, hi in ranges:
        m = cv2.inRange(hsv,
                        np.array([lo, s_lo, v_lo], dtype=np.uint8),
                        np.array([hi, s_hi, v_hi], dtype=np.uint8),
                        dst=None)
        mask = m if mask is None else cv2.bitwise_or(mask, m, dst=mask)
    if mask is None:
        return None, None
    if keep is not None:
        cv2.bitwise_and(mask, keep, dst=mask)
    # Морфология как в трекере: OPEN 3x3 (шум) + CLOSE 3x3 (дырки).
    k = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k)
    det = gate_cluster.detect_gate(mask, params)
    if det is None:
        return None, mask
    return float(det["radius"]), mask


def prepare_frame(frame_bgr, track_w, track_h, rotate):
    """Приводит кадр к рабочему размеру (как frame_from_raw в трекере)."""
    if frame_bgr is None or frame_bgr.size == 0:
        return None
    if rotate == 90:
        frame_bgr = cv2.rotate(frame_bgr, cv2.ROTATE_90_CLOCKWISE)
    elif rotate == 180:
        frame_bgr = cv2.rotate(frame_bgr, cv2.ROTATE_180)
    elif rotate == 270:
        frame_bgr = cv2.rotate(frame_bgr, cv2.ROTATE_90_COUNTERCLOCKWISE)
    if frame_bgr.shape[1] != track_w or frame_bgr.shape[0] != track_h:
        interp = cv2.INTER_AREA if frame_bgr.shape[1] >= track_w else cv2.INTER_LINEAR
        frame_bgr = cv2.resize(frame_bgr, (track_w, track_h), interpolation=interp)
    return frame_bgr


def keep_mask_for(track_w, track_h, use_self_mask):
    """Keep-маска корпуса робота (Idea 8) — как в рантайме, из настроек."""
    if not use_self_mask or SM is None:
        return None
    section, warn = SM.load_section(os.path.join(
        os.path.dirname(os.path.abspath(HERE)), "camera_settings.json"))
    if warn:
        print(f"[DIST-CAL] {warn}")
    frac = section if section is not None else {
        "enabled": True, "cx": 0.5, "cy": 0.5, "r": 0.22, "r2": 0.0}
    if not frac.get("enabled"):
        return None
    params = SM.params_from_fractions(track_w, track_h, frac)
    if not params.get("enabled") or params.get("r", 0) <= 0:
        return None
    return SM.build_keep_mask(params, track_w, track_h)


# ===================== режимы =====================
def open_live_camera(idx, track_w, track_h):
    """Локальная камера: авто-детект ELP (полный FOV) или явный индекс."""
    if fisheye is None:
        cap = cv2.VideoCapture(idx if idx is not None else 0)
        return cap if cap.isOpened() else None
    if idx is not None:
        return fisheye.open_camera(idx, track_w, track_h, 30)
    cap = fisheye.find_full_fov_camera()
    if cap is None:
        print("[DIST-CAL] ELP (полный FOV) не найден — открываю камеру 0")
        cap = fisheye.open_camera(0, track_w, track_h, 30)
    return cap


def do_live(args, sess, rgb, rgb_src, params, keep):
    """Живой цикл: измеряет r, Пробел записывает точку при текущей --dist."""
    cap = open_live_camera(args.camera, args.track_size[0], args.track_size[1])
    if cap is None:
        print("[DIST-CAL] камера не открылась — живой режим недоступен")
        return 1
    dist = args.dist
    print(f"[DIST-CAL] цвет {rgb} ({rgb_src}), допуск H,S,V={args.tol}, "
          f"кадр {args.track_size[0]}x{args.track_size[1]}")
    print(f"[DIST-CAL] текущая дистанция: {dist:.0f} см — Пробел записать, "
          f"[ / ] изменить на 10 см, f — фит, q — выход")
    last_print = 0.0
    try:
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                time.sleep(0.05)
                continue
            f = prepare_frame(frame, args.track_size[0], args.track_size[1],
                              args.rotate)
            if f is None:
                continue
            r, mask = measure_radius(f, rgb, args.tol, params, keep)
            now = time.monotonic()
            if now - last_print >= 1.0:
                last_print = now
                if r is None:
                    print(f"[DIST-CAL] d={dist:.0f} см — цвет не найден")
                else:
                    print(f"[DIST-CAL] d={dist:.0f} см -> r={r:.1f} px")
            if args.preview and mask is not None:
                cv2.imshow("dist_calibrate (mask)", mask)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord('q'), 27):
                break
            elif key == ord(' '):
                if r is None:
                    print("[DIST-CAL] цвет не найден — точка не записана")
                else:
                    sess["points"].append([float(dist), float(r)])
                    print(f"[DIST-CAL] + точка ({dist:.0f} см, {r:.1f} px), "
                          f"всего {len(sess['points'])}")
                    fit_and_save(sess, args.out, session_file=args.session)
            elif key == ord('['):
                dist = max(5.0, dist - 10.0)
                print(f"[DIST-CAL] дистанция: {dist:.0f} см")
            elif key == ord(']'):
                dist = dist + 10.0
                print(f"[DIST-CAL] дистанция: {dist:.0f} см")
            elif key == ord('f'):
                fit_and_save(sess, args.out, session_file=args.session)
    finally:
        cap.release()
        cv2.destroyAllWindows()
    return 0


def do_image(args, sess, rgb, rgb_src, params, keep):
    """Один сохранённый кадр -> одна точка."""
    frame = cv2.imread(args.image)
    if frame is None:
        print(f"[DIST-CAL] не удалось прочитать {args.image}")
        return 1
    f = prepare_frame(frame, args.track_size[0], args.track_size[1], args.rotate)
    r, _mask = measure_radius(f, rgb, args.tol, params, keep)
    if r is None:
        print(f"[DIST-CAL] цвет {rgb} ({rgb_src}) на кадре не найден — "
              f"точка не записана")
        return 1
    sess["points"].append([float(args.dist), float(r)])
    print(f"[DIST-CAL] + точка ({args.dist:.0f} см, {r:.1f} px), "
          f"всего {len(sess['points'])}")
    fit_and_save(sess, args.out, session_file=args.session)
    return 0


def do_add(args, sess):
    """Ручные точки (камера не нужна): --add dist_cm,r_px."""
    for spec in args.add:
        try:
            d_s, r_s = str(spec).replace(" ", "").split(",")
            d, r = float(d_s), float(r_s)
        except (TypeError, ValueError):
            print(f"[DIST-CAL] --add {spec!r}: ожидается dist_cm,r_px")
            return 1
        if d <= 0 or r <= 0:
            print(f"[DIST-CAL] --add {spec!r}: нужны положительные значения")
            return 1
        r = scale_r_to_session(r, args.frame_size[0], args.frame_size[1], sess)
        sess["points"].append([d, r])
        print(f"[DIST-CAL] + точка ({d:.1f} см, {r:.1f} px), всего {len(sess['points'])}")
    fit_and_save(sess, args.out, session_file=args.session)
    return 0


def do_remove(args, sess):
    """Удалить точки по номерам (с 1) — после --add, если оба заданы."""
    for n in sorted({int(v) for v in args.remove}, reverse=True):
        if 1 <= n <= len(sess["points"]):
            removed = sess["points"].pop(n - 1)
            print(f"[DIST-CAL] - точка №{n} ({removed[0]:.1f} см, "
                  f"{removed[1]:.1f} px)")
        else:
            print(f"[DIST-CAL] точки №{n} нет (всего {len(sess['points'])})")
    fit_and_save(sess, args.out, session_file=args.session)
    return 0


def do_report(sess, out_path):
    """--list/--check: модель + точки + невязки (ничего не меняет)."""
    pts = sess["points"]
    print(f"[DIST-CAL] точек в сессии: {len(pts)} "
          f"(кадр калибровки {sess['frame_w']}x{sess['frame_h'] or '?'})")
    for i, (d, r) in enumerate(pts):
        print(f"  #{i + 1}: d={d:.2f} см, r={r:.2f} px")
    try:
        with open(out_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        data = None
    if isinstance(data, dict) and data.get("a") is not None:
        m = D.model_from_dict(data)
        if m is not None and m.valid:
            print(f"[DIST-CAL] {os.path.basename(out_path)}: "
                  f"a={m.a:.6g} b={m.b:.6g} c={m.c:.6g}, валидна")
            if m.points:
                worst = 0.0
                for d, r in m.points:
                    pred = m.a / (m.b - r) - m.c
                    worst = max(worst, abs(pred - d))
                    print(f"  точка d={d:8.2f} -> предск. {pred:8.2f} "
                          f"(невязка {pred - d:+.2f} см)")
                print(f"[DIST-CAL] макс |невязка| = {worst:.2f} см")
            print(f"[DIST-CAL] вилка r калибровки: [{m.r_lo:.1f}..{m.r_hi:.1f}] px "
                  f"(рабочая: [{m.r_eval_lo:.1f}..{m.r_eval_hi:.1f}]), "
                  f"кадр {m.frame_w}x{m.frame_h}")
        else:
            print(f"[DIST-CAL] {os.path.basename(out_path)}: модель ОТКЛОНЕНА "
                  f"({getattr(m, 'error', 'формат')})")
    else:
        print(f"[DIST-CAL] {os.path.basename(out_path)}: калибровки нет")
    return 0


def build_parser():
    p = argparse.ArgumentParser(
        description="Idea 6: калибровка дистанции до ворот по размеру "
                    "(d = a/(b-r) - c -> distance_calib.json)")
    p.add_argument("--out", default=None,
                   help="путь distance_calib.json (по умолчанию рядом с "
                        "UniKostyl/distance.py)")
    p.add_argument("--session", default=None,
                   help="файл сессии точек (по умолчанию out/%s)" % SESSION_NAME)
    p.add_argument("--list", action="store_true",
                   help="показать модель и точки, ничего не менять")
    p.add_argument("--check", action="store_true",
                   help="то же, что --list (отчёт о качестве фита)")
    p.add_argument("--add", action="append", default=[], metavar="DIST_CM,R_PX",
                   help="добавить точку вручную (можно несколько раз)")
    p.add_argument("--remove", action="append", default=[], type=int, metavar="N",
                   help="удалить точку №N (с 1; можно несколько раз)")
    p.add_argument("--clear", action="store_true", help="удалить все точки")
    p.add_argument("--fit", action="store_true",
                   help="перефитить и записать distance_calib.json")
    p.add_argument("--image", metavar="FILE",
                   help="измерить r на сохранённом кадре (--dist обязателен)")
    p.add_argument("--live", action="store_true",
                   help="живая камера: Пробел — точка, [ ] — дистанция, q — выход")
    p.add_argument("--camera", type=int, default=None, metavar="IDX",
                   help="индекс камеры (по умолчанию авто-детект ELP)")
    p.add_argument("--preview", action="store_true",
                   help="окно с маской в живом режиме")
    p.add_argument("--dist", type=float, default=None, metavar="CM",
                   help="известная дистанция в см (для --image/--live)")
    p.add_argument("--rgb", default=None, metavar="R,G,B",
                   help="цвет ворот для измерения (по умолчанию G1 из "
                        "saved_gates.json, иначе жёлтый)")
    p.add_argument("--gate", type=int, default=1, metavar="N",
                   help="слот saved_gates.json для цвета (1..3, умолчание 1)")
    p.add_argument("--tol", default="40,100,100", metavar="H,S,V",
                   help="допуск HSV (как --tolerance у rpi_elp_cam.py)")
    p.add_argument("--track-size", default="640x360",
                   help="рабочий размер кадра измерения (по умолчанию 640x360)")
    p.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=0)
    p.add_argument("--frame-size", default="640x360",
                   help="размер кадра, в котором задан r ручных точек --add")
    p.add_argument("--no-self-mask", dest="self_mask_on", action="store_false",
                   help="не применять self-маску корпуса при измерении")
    p.set_defaults(self_mask_on=True)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        args.tol = tuple(int(v) for v in str(args.tol).replace(" ", "").split(","))
        if len(args.tol) != 3:
            raise ValueError
    except ValueError:
        print("[DIST-CAL] --tol должен быть H,S,V")
        return 2

    def parse_size(text):
        w, h = str(text).lower().replace(" ", "").split("x")
        return int(w), int(h)

    try:
        args.track_size = parse_size(args.track_size)
        args.frame_size = parse_size(args.frame_size)
    except Exception:
        print("[DIST-CAL] размер должен быть ШИРИНАxВЫСОТА")
        return 2

    out_path = args.out or D.default_path()
    sess = load_session(args.session)
    # Приоритет источника точек: существующий distance_calib.json > сессия
    # (добавление к уже откалиброванному набору работает из коробки).
    if sess["frame_w"] == 0 and sess["frame_h"] == 0:
        calib = points_from_calib(out_path)
        if calib is not None and calib["points"]:
            sess = calib

    if args.clear:
        sess = {"frame_w": args.frame_size[0], "frame_h": args.frame_size[1],
                "points": []}
        save_session(sess)
        print("[DIST-CAL] точки очищены")
        return 0

    if args.list or args.check:
        return do_report(sess, out_path)

    if not (args.add or args.remove or args.fit or args.image or args.live):
        return do_report(sess, out_path)

    tw, th = args.track_size
    # Масштаб сессии: размер измеренных кадров (для ручных точек — --frame-size).
    if sess["frame_w"] == 0:
        sess["frame_w"], sess["frame_h"] = tw, th

    if args.add:
        return do_add(args, sess)
    if args.remove:
        return do_remove(args, sess)
    if args.fit:
        fit_and_save(sess, out_path, session_file=args.session)
        return 0

    # Измерение (кадр/живая камера) — нужен цвет и дистанция.
    if args.dist is None or args.dist <= 0:
        print("[DIST-CAL] укажите известную дистанцию: --dist CM")
        return 2
    if args.rgb:
        try:
            rgb = tuple(int(v) for v in str(args.rgb).replace(" ", "").split(","))
            if len(rgb) != 3 or any(not (0 <= c <= 255) for c in rgb):
                raise ValueError
        except ValueError:
            print("[DIST-CAL] --rgb должен быть R,G,B (0..255)")
            return 2
        rgb_src = "CLI --rgb"
    else:
        rgb, rgb_src = gate_color(args)

    params = gate_cluster.resolve_params(tw, th)
    keep = keep_mask_for(tw, th, args.self_mask_on)
    if keep is not None:
        print("[DIST-CAL] self-маска корпуса применена при измерении (как в рантайме)")

    if args.image:
        return do_image(args, sess, rgb, rgb_src, params, keep)
    if args.live:
        return do_live(args, sess, rgb, rgb_src, params, keep)
    return do_report(sess, out_path)


if __name__ == "__main__":
    sys.exit(main())
