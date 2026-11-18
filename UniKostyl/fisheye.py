import os

import cv2
import numpy as np


def _attempts():
    # На Windows DSHOW не принимает MJPG у этой камеры (остаётся YUY2, ~5 fps),
    # а MSMF + MJPG даёт полный FOV 1080p на ~26 fps. V4L2 — только Linux.
    if os.name == "nt":
        return [
            (cv2.CAP_MSMF, "MJPG"),
            (cv2.CAP_MSMF, None),
            (cv2.CAP_ANY, None),
        ]
    return [
        (cv2.CAP_V4L2, "MJPG"),
        (cv2.CAP_V4L2, None),
        (cv2.CAP_ANY, None),
    ]


def open_camera(source, width=None, height=None, fps=None):
    if not isinstance(source, int):
        return cv2.VideoCapture(source)

    attempts = _attempts()
    for api, fourcc in attempts:
        print(f"Попытка открыть камеру {source} через API {api} (FOURCC: {fourcc})...")
        cap = cv2.VideoCapture(source, api)
        if not cap.isOpened():
            cap.release()
            continue
        if fourcc:
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*fourcc))
        if width:
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        if height:
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        if fps:
            cap.set(cv2.CAP_PROP_FPS, fps)

        ok, _ = cap.read()
        if ok:
            print(f"УСПЕХ: Камера открыта. Запрошено разрешение: {width}x{height}")
            return cap
        cap.release()

    print("ПРЕДУПРЕЖДЕНИЕ: Камера открыта, но чтение кадра не удалось. Пробуем дефолтные настройки.")
    return cv2.VideoCapture(source)


def find_full_fov_camera(max_index=6, width=1920, height=1080, preferred=(1,)):
    """Ищет индекс камеры, отдающей полный кадр.

    У ELP 180° полный угол обзора есть только в 1920x1080 (README_CAMERA.md),
    остальные режимы — кроп. Встроенные вебки 1080p не отдают, поэтому
    «кто смог 1920x1080» — надёжный признак именно ELP.

    preferred: индексы, которые пробуем ПЕРВЫМИ (known-good ELP на этом
    ноутбуке — индекс 1). Это быстрый путь: одна попытка открытия+чтения
    вместо последовательного перебора 0..5, где каждый отказ MSMF стоит
    секунд и на старте появляется долгий чёрный экран.
    """
    api = cv2.CAP_MSMF if os.name == "nt" else cv2.CAP_V4L2
    print(f"Ищу камеру с полным FOV {width}x{height} (индексы 0..{max_index - 1})...",
          flush=True)
    order = list(preferred) + [i for i in range(max_index) if i not in preferred]
    for index in order:
        print(f"  пробую индекс {index}...", flush=True)
        cap = cv2.VideoCapture(index, api)
        if not cap.isOpened():
            cap.release()
            continue
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, 30)
        ok, frame = cap.read()
        cap.release()
        if ok and frame is not None and frame.shape[1] == width and frame.shape[0] == height:
            print(f"Полный FOV {width}x{height} найден на индексе {index}.", flush=True)
            return index
    print("Камера с полным кадром не найдена. Если меню камеры уже запущено в другом окне — "
          "закройте его: камера работает монопольно (ошибка MSMF -1072875772 = устройство занято).")
    return None


def clean_mask(mask, ksize):
    if ksize > 0:
        kernel = np.ones((ksize, ksize), np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    return mask


def _fit16(w, h):
    """Вписать панель 16:9 в бокс w x h, не искажая пропорции."""
    pw = max(1, min(w, h * 16 // 9))
    return pw, max(1, pw * 9 // 16)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Yellow / blue gates renderer")
    parser.add_argument("--input", default="auto",
                        help="camera index, video path или 'auto' — найти камеру с полным FOV (1920x1080)")
    parser.add_argument("--width", type=int, default=1920, help="Ширина захвата с камеры")
    parser.add_argument("--height", type=int, default=1080, help="Высота захвата с камеры")
    args = parser.parse_args()

    if args.input.lower() == "auto":
        source = find_full_fov_camera()
        if source is None:
            raise SystemExit(
                "Камера с полным FOV 1920x1080 не найдена. "
                "Запустите `python UniKostyl/list_cams.py` и укажите индекс: --input N"
            )
    else:
        source = int(args.input) if args.input.isdigit() else args.input
    print(f"Запуск с параметрами: input={source}, width={args.width}, height={args.height}")

    cap = open_camera(source, args.width, args.height, 30)
    if not cap.isOpened():
        raise SystemExit(f"ОШИБКА: Не удалось открыть камеру {args.input}.")

    # --- НАСТРОЙКА ОКНА (всё в одном: gates + schematic + маска) ---
    window = "gates"
    cv2.namedWindow(window, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(window, 1200, 675)

    # Ползунки: имя -> (значение по умолчанию, максимум)
    sliders = {
        "gates 0=off 1=on": (0, 1),
        "blue H min": (100, 179),
        "blue H max": (135, 179),
        "yellow H min": (20, 179),
        "yellow H max": (40, 179),
        "S min": (40, 255),
        "V min": (40, 255),
        "clean ksize": (5, 21),
        "bg 0=blk 1=gray 2=orig": (1, 2),
    }
    for name, (val, vmax) in sliders.items():
        cv2.createTrackbar(name, window, val, vmax, lambda v: None)

    def reset_sliders():
        for name, (val, _vmax) in sliders.items():
            try:
                cv2.setTrackbarPos(name, window, val)
            except cv2.error:
                pass

    # Кнопка RESET рисуется на полотне (клик мышью); клавиша 'r' делает то же
    ui = {"btn": None, "reset": False, "flash": 0}

    def on_mouse(event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN and ui["btn"] is not None:
            x1, y1, x2, y2 = ui["btn"]
            if x1 <= x <= x2 and y1 <= y <= y2:
                ui["reset"] = True

    cv2.setMouseCallback(window, on_mouse)

    prev = cv2.getTickCount()
    fps = 0.0
    print("Вход в главный цикл. 'q' — выход, 's' — сохранить кадр, 'r' — сброс ползунков.")

    while True:
        ok, frame = cap.read()
        if not ok:
            print("ПРЕДУПРЕЖДЕНИЕ: Камера перестала отдавать кадры.")
            break

        try:
            gates_on = cv2.getTrackbarPos("gates 0=off 1=on", window)
            bh_min = cv2.getTrackbarPos("blue H min", window)
            bh_max = cv2.getTrackbarPos("blue H max", window)
            yh_min = cv2.getTrackbarPos("yellow H min", window)
            yh_max = cv2.getTrackbarPos("yellow H max", window)
            s_min = cv2.getTrackbarPos("S min", window)
            v_min = cv2.getTrackbarPos("V min", window)
            ksize = cv2.getTrackbarPos("clean ksize", window)
            mode = cv2.getTrackbarPos("bg 0=blk 1=gray 2=orig", window)
        except cv2.error:
            print("Окно закрыто пользователем.")
            break

        if bh_min > bh_max: bh_min, bh_max = bh_max, bh_min
        if yh_min > yh_max: yh_min, yh_max = yh_max, yh_min
        if ksize % 2 == 0: ksize += 1

        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

        # Только синяя и желтая маски
        blue_mask = cv2.inRange(hsv, (bh_min, s_min, v_min), (bh_max, 255, 255))
        yellow_mask = cv2.inRange(hsv, (yh_min, s_min, v_min), (yh_max, 255, 255))

        blue_mask = clean_mask(blue_mask, ksize)
        yellow_mask = clean_mask(yellow_mask, ksize)

        # Основное окно: при gates=off показываем чистый кадр камеры
        if not gates_on:
            out = frame.copy()
        elif mode == 0:
            out = np.zeros_like(frame)
        elif mode == 1:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            gray = (gray.astype(np.float32) * 0.3).astype(np.uint8)
            out = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        else:
            out = frame.copy()

        if gates_on:
            out[blue_mask > 0] = (255, 0, 0)
            out[yellow_mask > 0] = (0, 255, 255)

        now = cv2.getTickCount()
        dt = (now - prev) / cv2.getTickFrequency()
        prev = now
        if dt > 0:
            fps = 0.9 * fps + 0.1 * (1.0 / dt)

        cv2.putText(out, f"{fps:.1f} fps", (10, 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2, cv2.LINE_AA)

        # --- Схема (только ворота) ---
        schematic = np.zeros_like(frame)
        if gates_on:
            # Заливаем только желтые и синие ворота
            schematic[blue_mask > 0] = (255, 0, 0)      # Синий
            schematic[yellow_mask > 0] = (0, 255, 255)  # Желтый

            # Ищем контуры для ворот
            contours_blue, _ = cv2.findContours(blue_mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
            contours_yellow, _ = cv2.findContours(yellow_mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)

            # Рисуем белую обводку
            cv2.drawContours(schematic, contours_blue, -1, (255, 255, 255), 2)
            cv2.drawContours(schematic, contours_yellow, -1, (255, 255, 255), 2)

        # --- Сброс ползунков по кнопке или клавише ---
        if ui["reset"]:
            ui["reset"] = False
            reset_sliders()
            ui["flash"] = 10

        # --- Всё в одном окне; полотно под текущий размер окна, панели 16:9 без искажений ---
        try:
            _rx, _ry, W, H = cv2.getWindowImageRect(window)
        except cv2.error:
            W, H = 0, 0
        if W <= 0 or H <= 0:
            W, H = 1200, 450

        gap = 6
        btn_h = 30
        canvas = np.zeros((H, W, 3), dtype=np.uint8)
        right_w = max(220, int(W * 0.28))
        left_w = W - right_w - gap

        # gates — левая ячейка
        gw, gh = _fit16(left_w, H)
        gx, gy = (left_w - gw) // 2, (H - gh) // 2
        canvas[gy:gy + gh, gx:gx + gw] = cv2.resize(out, (gw, gh))

        # правая колонка: схема, под ней маска, внизу кнопка RESET
        x0 = left_w + gap
        cell_h = (H - 3 * gap - btn_h) // 2
        sw, sh = _fit16(right_w, cell_h)
        sx = x0 + (right_w - sw) // 2
        sy1 = gap + (cell_h - sh) // 2
        canvas[sy1:sy1 + sh, sx:sx + sw] = cv2.resize(schematic, (sw, sh))
        sy2 = gap + cell_h + gap + (cell_h - sh) // 2
        yellow_bgr = cv2.cvtColor(cv2.resize(yellow_mask, (sw, sh)), cv2.COLOR_GRAY2BGR)
        canvas[sy2:sy2 + sh, sx:sx + sw] = yellow_bgr

        btn_w = min(160, right_w - 10)
        bx = x0 + (right_w - btn_w) // 2
        by = gap + 2 * cell_h + 2 * gap
        ui["btn"] = (bx, by, bx + btn_w, by + btn_h)
        btn_color = (255, 255, 255) if ui["flash"] > 0 else (80, 80, 80)
        txt_color = (0, 0, 0) if ui["flash"] > 0 else (255, 255, 255)
        cv2.rectangle(canvas, (bx, by), (bx + btn_w, by + btn_h), btn_color, -1)
        (tw, th), _ = cv2.getTextSize("RESET", cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.putText(canvas, "RESET", (bx + (btn_w - tw) // 2, by + (btn_h + th) // 2),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, txt_color, 1, cv2.LINE_AA)

        label = (140, 140, 140)
        cv2.putText(canvas, "gates" if gates_on else "camera (gates off)", (gx + 8, gy + 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, label, 1, cv2.LINE_AA)
        cv2.putText(canvas, "schematic", (sx + 8, sy1 + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.45, label, 1, cv2.LINE_AA)
        cv2.putText(canvas, "yellow mask", (sx + 8, sy2 + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.45, label, 1, cv2.LINE_AA)

        if ui["flash"] > 0:
            ui["flash"] -= 1

        cv2.imshow(window, canvas)

        key = cv2.waitKey(1) & 0xFF
        if key == ord("q"):
            break
        if key == ord("r"):
            ui["reset"] = True
        if key == ord("s"):
            cv2.imwrite("gates_render.png", canvas)
            cv2.imwrite("gates_schematic.png", schematic)
            cv2.imwrite("gates_source.png", frame)
            print("Сохранены изображения.")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()