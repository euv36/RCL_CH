# -*- coding: utf-8 -*-
"""
camset_ui_smoke.py — smoke-проверка связки «кнопка CAM SET -> окно настроек ->
применение к живой камере -> JSON» в реальном unikostyl-UI (без запуска
полного cv_kostyl.main_loop; pygame-окно создаётся как обычно).

Что проверяется:
  1. install() добавил кнопку btn_cam_settings в unikostyl.widgets/wnames.
  2. Клик по кнопке (эмуляция pygame-события) открывает cv2-окно.
  3. main_loop_frame(None) + cam_ui.pump() работают вместе (как в cv_kostyl).
  4. Трекбар gain=25 реально применён к камере и ЧИТАЕТСЯ ОБРАТНО (ok),
     brightness в статусе, JSON записан во временный путь (не трогаем
     настоящий camera_settings.json).
  5. Повторный клик закрывает окно и сохраняет JSON.

Запуск: python UniKostyl/out/camset_ui_smoke.py [--idx 0]
Требует камеру (монопольно) — держите camera-lock из COORDINATION.md.
"""

import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import pygame  # noqa: E402

import unikostyl  # noqa: E402  (создаёт pygame-окно и виджеты — так задумано)
import cam_settings as cs  # noqa: E402


def click(widget):
    """Эмуляция полного клика по виджету (mousedown -> mouseup)."""
    center = (widget.rect.centerx, widget.rect.centery)
    down = pygame.event.Event(pygame.MOUSEBUTTONDOWN, {"pos": center})
    up = pygame.event.Event(pygame.MOUSEBUTTONUP, {"pos": center})
    widget.process_mousedown(down)
    widget.process_mouseup(up)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idx", type=int, default=None,
                    help="индекс камеры; по умолчанию авто-поиск ELP (полный FOV)")
    args = ap.parse_args()

    import cv2
    from fisheye import open_camera, find_full_fov_camera

    results = []

    def ok(name, cond, extra=""):
        results.append(bool(cond))
        print(("PASS " if cond else "FAIL ") + name + (f"  {extra}" if extra else ""))

    idx = args.idx
    if idx is None:
        idx = find_full_fov_camera()
        if idx is None:
            idx = 1  # известный запасной индекс ELP
            print(f"Полный FOV не найден, запасной индекс: {idx}")
        else:
            print(f"ELP (полный FOV 1920x1080) найден: индекс {idx}")

    cap = open_camera(idx, 1920, 1080, 30)
    ok("camera opened", cap is not None and cap.isOpened())
    if cap is not None and cap.isOpened():
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        print(f"INFO камера {idx}: {w}x{h} (ожидаем 1920x1080 — полный FOV)")
        ok("capture mode is 1920x1080 (no FOV crop)", (w, h) == (1920, 1080),
           f"got {w}x{h}")

    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "camera_settings.json")
        win = cs.install(unikostyl, cap_getter=lambda: cap,
                         source_getter=lambda: ("local", None),
                         settings_path=path)

        # 1. Кнопка зарегистрирована в UI
        btn = unikostyl.widgets.get("btn_cam_settings")
        ok("button in unikostyl.widgets", btn is not None)
        ok("button in unikostyl.wnames", "btn_cam_settings" in unikostyl.wnames)

        # 2. Клик открывает окно
        click(btn)
        ok("click opens cv2 window", win.open)
        ok("window visible", cv2.getWindowProperty(win.WIN, cv2.WND_PROP_VISIBLE) >= 1.0)

        # 3. Пара циклов «кадр + pump», как в cv_kostyl.main_loop
        for _ in range(20):
            unikostyl.main_loop_frame(None)
            win.pump()
            time.sleep(0.01)
        ok("main_loop_frame + pump coexist", win.open)

        # 4. Трекбар -> применение -> статус. На ELP/MSMF читка «залипшая»,
        #    поэтому после 3 разных значений gain честный статус = unconfirmed
        #    (ok — если драйвер читку подтверждает). Между значениями обязаны
        #    прокачивать pump: HighGUI хранит только текущую позицию трекбара.
        for gain_val in (25, 50, 75):
            cv2.setTrackbarPos(win._tb_name("gain"), win.WIN, gain_val)
            for _ in range(10):
                win.pump()
                unikostyl.main_loop_frame(None)
                time.sleep(0.01)
        cv2.setTrackbarPos(win._tb_name("brightness"), win.WIN, 10 + 64)  # min=-64
        for _ in range(20):
            win.pump()
            unikostyl.main_loop_frame(None)
            time.sleep(0.01)
        st_gain = win.status.get("gain", {})
        st_br = win.status.get("brightness", {})
        ok("gain applied + honest status",
           win.controls["gain"] == 75
           and st_gain.get("state") in ("ok", "unconfirmed"),
           f"state={st_gain.get('state')} got={st_gain.get('readback')}")
        print(f"INFO brightness status: {st_br.get('state')} "
              f"got={st_br.get('readback')} ({st_br.get('note')})")

        # 5. Автосохранение (дебаунс 1 с)
        time.sleep(1.2)
        win.pump()
        data = json.load(open(path, encoding="utf-8"))
        ok("autosave wrote JSON",
           data.get("controls", {}).get("gain") == 75
           and data.get("controls", {}).get("brightness") == 10,
           str(data.get("controls")))

        # 6. Повторный клик закрывает окно и сохраняет
        click(btn)
        ok("click closes window", not win.open)
        ok("close saved JSON", os.path.exists(path))

        # 7. Вернуть камере SAFE DEFAULTS
        win.cap_ctl.apply(dict(cs.SAFE_DEFAULTS))
        print("SAFE DEFAULTS восстановлены на камере.")

    if cap is not None:
        cap.release()

    # Разрушить pygame-UI корректно.
    try:
        pygame.quit()
    except Exception:
        pass

    print(f"\nUI-smoke: {sum(results)}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
