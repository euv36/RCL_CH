# -*- coding: utf-8 -*-
"""
camset_check.py — проверка Idea 1 (окно настроек камеры) на ПК.

Режимы:
  --json       camera-free: атомарная запись/чтение camera_settings.json,
               клампы, сохранность чужих секций (self_mask и т.п.), битый JSON,
               сборка Pi-сообщения. Без камеры и без окна. (по умолчанию)
  --gui        camera-free: открыть окно настроек на 3 с, программно двигать
               трекбары, проверить применение состояния и JSON-сохранение.
               Не трогает настоящую камеру и настоящий camera_settings.json
               (пишет во временный файл).
  --cam N      живая камера (нужен монопольный доступ!): открыть, применить
               brightness -64 / 0 / +64 с чтением обратно, сравнить среднюю
               яркость кадров (настройка обязана МЕНЯТЬ КАРТИНКУ), таблица
               readback по всем контролам, затем вернуть SAFE DEFAULTS.
  --auto       в паре с --cam: найти камеру с полным FOV (как fisheye --input auto).
  --idx N      индекс камеры для --cam (по умолчанию 0).

Запуск из корня репозитория:  python UniKostyl/out/camset_check.py --json
(если модуль cam_settings не найден: set PYTHONPATH=UniKostyl)
"""

import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import cam_settings as cs  # noqa: E402


def check_json():
    """Камера не нужна: круговорот JSON + крайние случаи."""
    results = []

    def ok(name, cond, extra=""):
        results.append((name, bool(cond), extra))
        print(("PASS " if cond else "FAIL ") + name + (f"  {extra}" if extra else ""))

    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "camera_settings.json")

        # 1. Файла нет -> load возвращает None
        ok("load(missing) is None", cs.load_settings(path) is None)

        # 2. Сохранение дефолтов и чтение обратно
        ctrl = dict(cs.SAFE_DEFAULTS)
        cs.save_settings(path, ctrl)
        data = json.load(open(path, encoding="utf-8"))
        loaded = cs.load_settings(path)
        ok("round-trip equals",
           loaded == ctrl,
           f"ts={data.get('ts')} keys={sorted(data.keys())}")

        # 3. Атомарность: в каталоге не остаётся tmp-файлов
        leftovers = [f for f in os.listdir(d) if ".tmp" in f]
        ok("no tmp leftovers", not leftovers, str(leftovers))

        # 4. Сохранность чужих секций (self_mask из Idea 8 и т.п.)
        data["self_mask"] = {"inner_r_pct": 25.0}
        data["controls"]["exposure"] = 400
        json.dump(data, open(path, "w", encoding="utf-8"))
        cs.save_settings(path, {"gain": 5})
        data = json.load(open(path, encoding="utf-8"))
        ok("foreign section preserved",
           data.get("self_mask", {}).get("inner_r_pct") == 25.0)
        ok("unknown controls merged (exposure=400 kept, gain=5 added)",
           data["controls"].get("exposure") == 400
           and data["controls"].get("gain") == 5)

        # 5. Кламп: значение вне диапазона записывается обрезанным
        cs.save_settings(path, {"gain": 999, "brightness": -500})
        data = json.load(open(path, encoding="utf-8"))
        ok("clamp on save",
           data["controls"]["gain"] == 100 and data["controls"]["brightness"] == -64)

        # 6. Битый JSON -> load None (не роняет вызывающего)
        open(path, "w", encoding="utf-8").write("{oops")
        ok("load(corrupt) is None", cs.load_settings(path) is None)

        # 7. load с чужими/битыми значениями контролов: неизвестное имя
        #    отбрасывается, нечисловое значение дезинфицируется в дефолт.
        open(path, "w", encoding="utf-8").write(
            json.dumps({"controls": {"exposure": 300, "hack": 1,
                                     "gain": "not-a-number"}}))
        loaded = cs.load_settings(path)
        ok("load filters unknown + sanitizes bad values",
           loaded == {"exposure": 300, "gain": 0}, str(loaded))

    # 8. Pi-сообщение: только известные имена, клампы
    msg = json.loads(cs.build_pi_message({"exposure": 300, "hack": 7, "gain": 250}))
    ok("pi message filters+clamps",
       msg["controls"] == {"exposure": 300, "gain": 100} and msg["cmd"] == "set",
       str(msg["controls"]))

    # 9. build_pi_message для cmd=get
    ok("pi message get",
       json.loads(cs.build_pi_message({}, cmd="get"))["cmd"] == "get")

    failed = [r for r in results if not r[1]]
    print(f"\nJSON-check: {len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


def check_gui():
    """Окно без камеры: trackbars, применение состояния, автосохранение."""
    import cv2

    failed = False
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "camera_settings.json")

        # A. cap_getter есть, но камера не открыта (как cv_kostyl после
        #    отключения локальной камеры): backend=local, статус nocam.
        win = cs.CameraSettingsWindow(cap_getter=lambda: None, picam=None,
                                      settings_path=path)
        win.open_window()
        print(f"Окно A открыто (backend={win.backend}). Двигаю трекбары программно...")

        # Программно двигаем трекбары, как это делал бы пользователь.
        cv2.setTrackbarPos(win._tb_name("gain"), win.WIN, 30)
        cv2.setTrackbarPos(win._tb_name("exposure"), win.WIN, 400 - 1)  # min=1
        for _ in range(30):
            win.pump()
            time.sleep(0.01)

        ok1 = win.controls["gain"] == 30 and win.controls["exposure"] == 400
        print(("PASS " if ok1 else "FAIL ") + f"trackbar -> state {win.controls}")

        st = win.status.get("gain", {})
        ok2 = (win.backend == "local" and st.get("state") == "nocam")
        print(("PASS " if ok2 else "FAIL ")
              + f"backend={win.backend}, no-camera state = {st.get('state')} "
                f"({st.get('note')})")

        win._save_now()
        data = json.load(open(path, encoding="utf-8"))
        ok3 = data["controls"]["gain"] == 30 and data["controls"]["exposure"] == 400
        print(("PASS " if ok3 else "FAIL ") + f"saved JSON {data['controls']}")

        win.close()
        print("Окно A закрыто.")
        failed = failed or not (ok1 and ok2 and ok3)

        # B. Без cap_getter вообще (модуль открыт без камеры): backend=pi,
        #    канала нет -> честное n/a, не молчание.
        win2 = cs.CameraSettingsWindow(cap_getter=None, picam=None,
                                       settings_path=path)
        win2.open_window()
        print(f"Окно B открыто (backend={win2.backend}).")
        cv2.setTrackbarPos(win2._tb_name("gamma"), win2.WIN, 200 - 72)  # min=72
        for _ in range(20):
            win2.pump()
            time.sleep(0.01)
        st2 = win2.status.get("gamma", {})
        ok4 = (win2.backend == "pi" and st2.get("state") == "n/a")
        print(("PASS " if ok4 else "FAIL ")
              + f"backend={win2.backend}, no-pi-channel state = {st2.get('state')} "
                f"({st2.get('note')})")
        win2.close()
        print("Окно B закрыто.")
        failed = failed or not ok4

    return 1 if failed else 0


def mean_gray(cap, n=12):
    import cv2
    import numpy as np

    vals = []
    for _ in range(n):
        ret, frame = cap.read()
        if not ret:
            time.sleep(0.05)
            continue
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        vals.append(float(np.mean(gray)))
        time.sleep(0.02)
    return sum(vals) / len(vals) if vals else None


def check_cam(index):
    """Живая проверка: применение + чтение обратно + реальное изменение кадра.

    Камера открывается ровно как в cv_kostyl.py (fix 2026-11-17): ELP ищется
    по полному FOV 1920x1080, режим захвата 1920x1080 MJPG (ниже — кроп).
    """
    import cv2

    from fisheye import open_camera

    print(f"Открываю камеру {index} (1920x1080 MJPG — полный FOV, как в cv_kostyl)...")
    cap = open_camera(index, 1920, 1080, 30)
    if not cap.isOpened():
        print("FAIL не смог открыть камеру (занята? запущен ли fisheye_test?)")
        return 1

    try:
        backend = cap.getBackendName()
    except Exception:
        backend = "?"
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    print(f"ОТКРЫТА: индекс {index}, бэкенд {backend}, фактический режим {w}x{h}")
    if (w, h) != (1920, 1080):
        print("WARN режим не 1920x1080 — у ELP это кроп, FOV неполный")

    ctl = cs.LocalCameraCtl(lambda: cap)
    failed = False
    try:
        # 1. Калибровка читки: на ELP/MSMF cap.get() «залипает» (одни и те же
        #    ответы на разные запросы — железный факт 2026-11-17), поэтому
        #    сперва прогоняем по 3 разных значения на контрол и смотрим,
        #    какие контролы вообще способны подтвердить применение.
        print("\n--- калибровка читки (по 3 значения на контрол) ---")
        for name, ctrl in cs.CONTROLS.items():
            if ctrl["cap"] is None:
                continue
            lo, hi = ctrl["min"], ctrl["max"]
            for probe in (lo, (lo + hi) // 2, hi):  # 3 РАЗНЫХ значения
                ctl.apply({name: probe})
                time.sleep(0.05)
        print("  контролы с ненадёжной читкой (unconfirmed):",
              sorted(ctl._unreliable) or "нет")

        print("\n--- apply + readback (SAFE DEFAULTS, после калибровки) ---")
        res = ctl.apply(dict(cs.SAFE_DEFAULTS))
        for name, st in res.items():
            print(f"  {name:<26s} req={st['requested']:>5} "
                  f"got={'-' if st['readback'] is None else round(st['readback'], 1):>7} "
                  f"[{st['state']}] {st.get('note', '')}")
            if st["state"] in ("fail",):
                failed = True

        # 2. Яркость кадра обязана меняться от brightness (иначе MSMF молчит).
        print("\n--- влияние brightness на картинку ---")
        base = mean_gray(cap)
        ctl.apply({"brightness": -64})
        dark = mean_gray(cap)
        ctl.apply({"brightness": 64})
        bright = mean_gray(cap)
        ctl.apply({"brightness": 0})
        print(f"  mean gray: brightness=0 -> {base:.1f}; -64 -> {dark:.1f}; "
              f"+64 -> {bright:.1f}")
        changed = (bright - dark) > 5.0
        print(("PASS " if changed else "FAIL ")
              + f"картинка реагирует на brightness (размах {bright - dark:.1f})")
        if not changed:
            failed = True

        # 3. Readback после установки крайних значений. На ELP/MSMF читка
        #    brightness «залипла» (всегда 0.0) — это железный факт 2026-11-17:
        #    честный статус unconfirmed (применено, не подтверждено).
        #    ok тоже принимаем (драйвер, который читку подтверждает).
        rb = ctl.apply({"brightness": -64})
        st = rb["brightness"]
        ok3 = st["state"] in ("ok", "unconfirmed")
        print(("PASS " if ok3 else "FAIL ")
              + f"brightness=-64 статус {st['state']} (readback {st['readback']}) — "
                f"реальное доказательство применения: тест картинки выше")
        if not ok3:
            failed = True

        # 4. Вернуть SAFE DEFAULTS (камера остаётся в проверенном состоянии).
        ctl.apply(dict(cs.SAFE_DEFAULTS))
        print("SAFE DEFAULTS восстановлены (exposure 300 UVC; на MSMF читка "
              "exposure залипшая, реальное состояние см. в окне).")
    finally:
        cap.release()

    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description="Проверка Idea 1 (cam_settings)")
    ap.add_argument("--json", action="store_true", help="camera-free JSON-тесты")
    ap.add_argument("--gui", action="store_true", help="окно без камеры, 3 с")
    ap.add_argument("--cam", action="store_true", help="живая камера (эксклюзивно!)")
    ap.add_argument("--idx", type=int, default=None,
                    help="индекс камеры; по умолчанию авто-поиск по полному FOV")
    ap.add_argument("--auto", action="store_true",
                    help="(по умолчанию) найти камеру с полным FOV автоматически")
    args = ap.parse_args()

    if args.cam:
        idx = args.idx
        if idx is None:
            # Как в cv_kostyl.py (fix 2026-11-17): ELP = камера с полным FOV.
            from fisheye import find_full_fov_camera
            idx = find_full_fov_camera()
            if idx is None:
                idx = 1  # известный запасной индекс ELP
                print(f"Полный FOV не найден, запасной индекс: {idx}")
            else:
                print(f"ELP (полный FOV 1920x1080) найден: индекс {idx}")
        sys.exit(check_cam(idx))
    if args.gui:
        sys.exit(check_gui())
    sys.exit(check_json())


if __name__ == "__main__":
    main()
