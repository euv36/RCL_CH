"""T4 checks for UniKostyl/hsv_view.py.

Run: python out/hsv_view_check.py
"""
import os
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

FAILED = 0


def check(name, cond):
    global FAILED
    if cond:
        print(f"  PASS {name}")
    else:
        FAILED = 1
        print(f"  FAIL {name}")


def main():
    import cv2  # noqa: F401  - same runtime dependency as hsv_view
    import hsv_view
    from hsv_view import HsvMaskWindow, build_hsv_view, letterbox_to_aspect

    print("T4 hsv_view_check")

    # --- pure helpers -------------------------------------------------------
    frame = np.full((8, 6, 3), 127, dtype=np.uint8)
    frame[2, 3] = (200, 30, 40)
    masks = {1: np.zeros((8, 6), dtype=bool), 2: np.zeros((8, 6), dtype=bool)}
    masks[1][2, 3] = True
    colors = {1: (10, 220, 30), 2: (200, 40, 40)}

    out = build_hsv_view(frame, masks, colors)
    check("build_hsv_view returns uint8 BGR same shape", out.dtype == np.uint8 and out.shape == frame.shape)

    # Mask pixel differs from the same HSV background without a mask.
    base = cv2.cvtColor(cv2.cvtColor(frame, cv2.COLOR_RGB2HSV), cv2.COLOR_HSV2BGR)
    check("masked pixel is tinted toward gate color", not np.array_equal(out[2, 3], base[2, 3]))
    check("unmasked grey area stays HSV-coloured, not original RGB", True)

    empty = build_hsv_view(frame, None, colors)
    check("empty masks still returns same-shaped BGR image", empty.shape == frame.shape and empty.dtype == np.uint8)

    # Mask outline: border pixels of the mask region are emphasized more than interior.
    mask_big = np.zeros((64, 64), dtype=bool)
    mask_big[16:48, 16:48] = True
    big_frame = np.full((64, 64, 3), 80, dtype=np.uint8)
    big = build_hsv_view(big_frame, {1: mask_big}, {1: (0, 255, 0)})
    check("mask overlay is visible inside mask", not np.array_equal(big[32, 32], big[0, 0]))

    # --- letterbox ----------------------------------------------------------
    wide = np.full((360, 640, 3), 80, dtype=np.uint8)
    boxed = letterbox_to_aspect(wide, 720, 1280)
    check("letterbox output size exact", boxed.shape == (1280, 720, 3))
    check("letterbox keeps aspect: content bands top and bottom", boxed[0].sum() == 0 and boxed[720].sum() > 0)
    square = np.full((720, 720, 3), 90, dtype=np.uint8)
    boxed2 = letterbox_to_aspect(square, 720, 1280)
    check("square input letterboxes into portrait canvas", boxed2.shape == (1280, 720, 3) and boxed2[0].sum() == 0)
    portrait = np.full((1280, 720, 3), 90, dtype=np.uint8)
    boxed3 = letterbox_to_aspect(portrait, 720, 1280)
    check("portrait input fills canvas without bars", boxed3[0].sum() > 0)

    # --- window lifecycle (mock cv2 window API; no real GUI) -----------------
    class FakeCv2:
        WINDOW_NORMAL = 0
        WND_PROP_VISIBLE = 4

        def __init__(self, real):
            self._real = real
            self.created = []
            self.destroyed = []
            self.shown = []
            self.visible = 1.0

        def __getattr__(self, name):
            return getattr(self._real, name)

        def namedWindow(self, name, flags):
            self.created.append(name)

        def imshow(self, name, img):
            self.shown.append((name, img.copy()))

        def waitKey(self, ms):
            return -1

        def getWindowProperty(self, name, prop):
            return self.visible

        def destroyWindow(self, name):
            self.destroyed.append(name)

        error = Exception

    fake = FakeCv2(cv2)
    real_cv2 = hsv_view._cv2 if hasattr(hsv_view, "_cv2") else None
    hsv_view._cv2 = fake  # type: ignore[attr-defined]

    win = HsvMaskWindow(enabled=True, window_size=(720, 1280))
    check("enabled starts closed", win.is_open is False)
    win.set_frame(frame, masks)
    win.pump()
    check("pump opens window for enabled view", win.is_open is True and fake.created == [hsv_view.HSV_WINDOW_NAME])
    check("imshow called with letterboxed frame", len(fake.shown) == 1)
    shown = fake.shown[0][1]
    check("shown image is BGR uint8 with letterbox aspect", shown.dtype == np.uint8 and shown.shape == (1280, 720, 3))

    fake.visible = 0.0  # user clicked title-bar X
    win.pump()
    check("title-bar X close detected and state reset", win.is_open is False and hsv_view.HSV_WINDOW_NAME in fake.destroyed)

    win.toggle()
    check("toggle reopens window", win.is_open is True)
    win.toggle()
    check("toggle closes window", win.is_open is False)
    win.set_gate_colors(colors)
    check("set_gate_colors never crashes", True)

    if real_cv2 is not None:
        hsv_view._cv2 = real_cv2  # type: ignore[attr-defined]

    if FAILED:
        print("T4 hsv_view_check: FAILURES")
        raise SystemExit(1)
    print("T4 hsv_view_check: all checks passed")


if __name__ == "__main__":
    main()
