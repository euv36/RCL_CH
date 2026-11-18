"""HSV + gate-mask inspection window for the laptop UI.

Pure image builder is separated from the cv2 window lifecycle so tests can
verify alignment without a display.
"""
import cv2 as _cv2
import numpy as np

HSV_WINDOW_NAME = "HSV MASK"

_DEFAULT_COLORS = {
    1: (255, 255, 0),   # BGR-friendly yellow (RGB tuple kept for callers)
    2: (0, 128, 255),
    3: (255, 128, 0),
}


def _bgr(color):
    return (int(color[2]), int(color[1]), int(color[0]))


def _as_mask(mask, shape):
    if mask is None:
        return None
    arr = np.asarray(mask)
    if arr.shape != shape[:2]:
        return None
    return arr.astype(bool, copy=False)


def build_hsv_view(frame_rgb, masks, gate_colors):
    """Return BGR uint8: HSV view with selected-gate masks tinted/outlined."""
    if frame_rgb is None:
        frame_rgb = np.zeros((720, 1280, 3), dtype=np.uint8)
    frame = np.asarray(frame_rgb, dtype=np.uint8)
    if frame.ndim != 3 or frame.shape[2] != 3:
        frame = np.zeros((720, 1280, 3), dtype=np.uint8)
    hsv = _cv2.cvtColor(frame, _cv2.COLOR_RGB2HSV)
    out = _cv2.cvtColor(hsv, _cv2.COLOR_HSV2BGR).astype(np.float32)

    if gate_colors is None:
        gate_colors = _DEFAULT_COLORS
    for gid, mask in (masks or {}).items():
        m = _as_mask(mask, frame.shape)
        if m is None or not m.any():
            continue
        color = np.array(_bgr(gate_colors.get(gid, _DEFAULT_COLORS.get(gid, (255, 255, 255)))), dtype=np.float32)
        out[m] = out[m] * 0.55 + color * 0.45
        # Outline gives a crisp edge without moving the mask plane.
        kernel = np.ones((3, 3), dtype=np.uint8)
        erode = _cv2.erode(m.astype(np.uint8), kernel, iterations=1).astype(bool)
        edge = m & ~erode
        out[edge] = color

    return np.clip(out, 0, 255).astype(np.uint8)


def letterbox_to_aspect(image_bgr, target_w, target_h):
    """Fit image into target canvas preserving aspect (black bars, no stretch)."""
    if image_bgr is None:
        image_bgr = np.zeros((target_h, target_w, 3), dtype=np.uint8)
    src_h, src_w = image_bgr.shape[:2]
    scale = min(float(target_w) / max(1, src_w), float(target_h) / max(1, src_h))
    new_w = max(1, int(round(src_w * scale)))
    new_h = max(1, int(round(src_h * scale)))
    resized = _cv2.resize(image_bgr, (new_w, new_h), interpolation=_cv2.INTER_LINEAR)
    canvas = np.zeros((target_h, target_w, 3), dtype=np.uint8)
    x = (target_w - new_w) // 2
    y = (target_h - new_h) // 2
    canvas[y:y + new_h, x:x + new_w] = resized
    return canvas


class HsvMaskWindow:
    def __init__(self, enabled=False, window_size=(720, 1280)):
        self.enabled = bool(enabled)
        self.window_size = tuple(window_size)
        self._frame_rgb = None
        self._masks = None
        self._gate_colors = dict(_DEFAULT_COLORS)
        self._win_open = False

    @property
    def is_open(self):
        return self._win_open

    def set_frame(self, frame_rgb, masks):
        self._frame_rgb = frame_rgb
        self._masks = masks

    def set_gate_colors(self, colors):
        if colors:
            self._gate_colors = dict(colors)

    def toggle(self):
        self.enabled = not self.enabled
        if self.enabled:
            self._ensure_window()
        else:
            self.close()
        return self.enabled

    def close(self):
        if self._win_open:
            try:
                _cv2.destroyWindow(HSV_WINDOW_NAME)
            except Exception:
                pass
        self._win_open = False
        self.enabled = False

    def pump(self, do_waitkey=True):
        if not self.enabled:
            return
        self._ensure_window()
        try:
            visible = _cv2.getWindowProperty(HSV_WINDOW_NAME, _cv2.WND_PROP_VISIBLE)
            if visible < 0.5:
                self.close()
                return
        except Exception:
            # Some backends throw after the user clicked the title-bar X.
            self.close()
            return

        image = build_hsv_view(self._frame_rgb, self._masks, self._gate_colors)
        box = letterbox_to_aspect(image, self.window_size[0], self.window_size[1])
        self._draw_legend(box)
        _cv2.imshow(HSV_WINDOW_NAME, box)
        if do_waitkey:
            try:
                key = _cv2.waitKey(1) & 0xFF
            except Exception:
                key = -1
            if key == 27:  # ESC
                self.close()

    def _ensure_window(self):
        if self._win_open:
            return
        _cv2.namedWindow(HSV_WINDOW_NAME, _cv2.WINDOW_NORMAL)
        try:
            _cv2.resizeWindow(HSV_WINDOW_NAME, self.window_size[0], self.window_size[1])
        except Exception:
            pass
        self._win_open = True

    def _draw_legend(self, image_bgr):
        y = 24
        for gid in sorted(self._gate_colors):
            color = _bgr(self._gate_colors[gid])
            _cv2.circle(image_bgr, (18, y), 7, color, -1, lineType=_cv2.LINE_AA)
            _cv2.putText(image_bgr, f"G{gid}", (32, y + 5), _cv2.FONT_HERSHEY_SIMPLEX, 0.5, (240, 240, 240), 1, _cv2.LINE_AA)
            y += 24


__all__ = ["HSV_WINDOW_NAME", "HsvMaskWindow", "build_hsv_view", "letterbox_to_aspect"]
