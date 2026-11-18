"""Перебор USB-камер: сохраняет пробный кадр с каждого индекса.

Запуск:
    python UniKostyl/list_cams.py            # индексы 0..5
    python UniKostyl/list_cams.py --max 8

Кадры сохраняются в UniKostyl/out/cam_probe_idx<N>.jpg — по ним видно,
какая камера на каком индексе (ELP 180° рыбий глаз сильно искажает).
"""
import argparse
import os

import cv2

# ELP 180° даёт полный угол обзора только в 1920x1080 (см. README_CAMERA.md)
PROBE_WIDTH = 1920
PROBE_HEIGHT = 1080


def backends():
    if os.name == "nt":
        # DSHOW у cv2 5.0 не открывается по индексу и не принимает MJPG — только MSMF
        return [cv2.CAP_MSMF]
    return [cv2.CAP_V4L2]


def try_open(index, api):
    cap = cv2.VideoCapture(index, api)
    if not cap.isOpened():
        cap.release()
        return None
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, PROBE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, PROBE_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, 30)
    # Первые кадры часто чёрные (камера прогревается) — берём пятый
    frame = None
    for _ in range(5):
        ok, frame = cap.read()
        if not ok:
            frame = None
    if frame is None:
        cap.release()
        return None
    return cap, frame


def main():
    parser = argparse.ArgumentParser(description="Проба USB-камер: индекс, разрешение, кадр")
    parser.add_argument("--max", type=int, default=6, help="сколько индексов перебирать (0..max-1)")
    args = parser.parse_args()

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(out_dir, exist_ok=True)

    found = 0
    for index in range(args.max):
        for api in backends():
            result = try_open(index, api)
            if result is None:
                continue
            cap, frame = result
            h, w = frame.shape[:2]
            fourcc = int(cap.get(cv2.CAP_PROP_FOURCC))
            fourcc = "".join(chr((fourcc >> 8 * i) & 0xFF) for i in range(4)).strip()
            fps = cap.get(cv2.CAP_PROP_FPS)
            path = os.path.join(out_dir, f"cam_probe_idx{index}.jpg")
            cv2.imwrite(path, frame)
            print(f"Индекс {index}: {w}x{h} @ {fps:.0f} fps, FOURCC={fourcc or '?'} -> {path}")
            cap.release()
            found += 1
            break
        else:
            print(f"Индекс {index}: не открылся ни через один бэкенд")

    if not found:
        print("Камер не найдено.")
    else:
        print("Посмотрите сохранённые кадры и выберите индекс нужной камеры.")


if __name__ == "__main__":
    main()
