#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scene_check.py — один кадр с ELP для визуальной проверки сцены.

Пользователь положил рядом с камерой красную папку и зелёное полотенце.
Скрипт открывает локальную камеру (автодетект 1920x1080 MJPG, известный
запасной вариант — индекс 1), снимает ОДИН кадр и сохраняет PNG.
Не входит в пайплайн — только ручная проверка сцены.
"""
import sys

import cv2 as cv

def main():
    last_err = None
    for idx in (1, 0, 2, 3):
        cap = cv.VideoCapture(idx, cv.CAP_MSMF)
        if not cap.isOpened():
            last_err = f"index {idx}: MSMF open failed"
            continue
        cap.set(cv.CAP_PROP_FOURCC, cv.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv.CAP_PROP_FRAME_WIDTH, 1920)
        cap.set(cv.CAP_PROP_FRAME_HEIGHT, 1080)
        ok, frame = cap.read()
        if not ok or frame is None:
            last_err = f"index {idx}: read failed"
            cap.release()
            continue
        h, w = frame.shape[:2]
        out = "out/scene_red_folder.png"
        cv.imwrite(out, frame)
        print(f"index {idx}: {w}x{h} -> {out}")
        cap.release()
        return 0
    print(f"no local camera: {last_err}")
    return 1

if __name__ == "__main__":
    sys.exit(main())
