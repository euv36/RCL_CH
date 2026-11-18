"""Офлайн-проверка мультитрекинга и оценки дистанции (без Pygame/камеры).

Рисует синтетический кадр 640x360 с двумя цветными пятнами, прогоняет ту же
логику, что и в unikostyl.main_loop_frame (mask -> contours -> angle -> r_px
-> distance) и печатает результаты. Проверяет:
  * 3 слота отслеживаются одновременно;
  * углы считаются от центра против часовой (0° = вправо);
  * для слота в зоне считается примерная реальная дистанция.
"""
import math

import cv2
import numpy as np

TRACK_COLORS = ((255, 255, 0), (0, 128, 255), (255, 128, 0))  # жёлтый, синий, оранжевый (RGB)
ZONE_RADIUS_FRAC = 0.25
KNOWN_TARGET_WIDTH_CM = 4.2
TARGET_MIN_AREA = 300
FOCAL_DIVISOR = math.pi
TRACK_SLOTS = (0, 1, 2)

W, H = 640, 360
frame = np.zeros((H, W, 3), dtype=np.uint8)
cx0, cy0 = W // 2, H // 2
# Гейт 1 (жёлтый) справа от центра на расстоянии 60 px
cv2.circle(frame, (cx0 + 60, cy0), 12, (255, 255, 0), -1)
# Гейт 2 (синий) сверху слева от центра (вверх = +90°)
cv2.circle(frame, (cx0, cy0 - 70), 12, (0, 128, 255), -1)
# Гейт 3 (оранжевый, цель) справа-вверх близко к центру
cv2.circle(frame, (cx0 + 40, cy0 - 30), 15, (255, 128, 0), -1)

LAB = cv2.cvtColor(frame, cv2.COLOR_RGB2LAB).astype(np.int16)


def rgb2lab_pixels(img):
    lab = cv2.cvtColor(img, cv2.COLOR_RGB2LAB).astype(np.int16)
    lab[:, :, 0] = lab[:, :, 0] * 100 // 255
    lab[:, :, 1] -= 128
    lab[:, :, 2] -= 128
    return lab.astype(np.int8)


pixels_LAB = rgb2lab_pixels(frame)
# Порог каждого слота = точный цвет пятна (сузим до ±25 по A/B, ±40 по L)
def make_thr(rgb):
    lab = rgb2lab_pixels(np.array([[rgb]], dtype=np.uint8))[0, 0]
    l, a, b = int(lab[0]), int(lab[1]), int(lab[2])
    return np.array([max(0, l - 40), min(100, l + 40),
                     max(-128, a - 25), min(127, a + 25),
                     max(-128, b - 25), min(127, b + 25)], dtype=np.int16)


thresholds = [make_thr(TRACK_COLORS[0]), make_thr(TRACK_COLORS[1]), make_thr(TRACK_COLORS[2])]
zone_radius = int(ZONE_RADIUS_FRAC * min(H, W))
print(f"zone_radius = {zone_radius} px (min(H,W)={min(H, W)})")

for i, slot in enumerate(TRACK_SLOTS):
    t = thresholds[slot]
    ind = np.all(pixels_LAB >= t[::2], axis=2) & np.all(pixels_LAB <= t[1::2], axis=2)
    gray = (ind * 255).astype(np.uint8)
    contours, _ = cv2.findContours(gray, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        print(f"G{slot + 1}: не найден")
        continue
    cnt = max(contours, key=cv2.contourArea)
    area = cv2.contourArea(cnt)
    if area <= TARGET_MIN_AREA:
        print(f"G{slot + 1}: слишком мелкий ({area:.0f})")
        continue
    M = cv2.moments(cnt)
    cxf, cyf = M["m10"] / M["m00"], M["m01"] / M["m00"]
    x, y, bw, bh = cv2.boundingRect(cnt)
    dx, dy = cxf - cx0, cy0 - cyf
    angle = int(math.degrees(math.atan2(dy, dx)) % 360)
    r_px = math.hypot(dx, dy)
    in_zone = r_px <= zone_radius
    focal = W / FOCAL_DIVISOR
    dist_cm = KNOWN_TARGET_WIDTH_CM * focal / max(bw, 1)
    print(f"G{slot + 1}: угол={angle}°, центр=({cxf:.0f},{cyf:.0f}), "
          f"r={r_px:.0f}px, ширина={bw}px, площадь={area:.0f}, "
          f"в_зоне={'да' if in_zone else 'нет'}, "
          f"дистанция≈{dist_cm / 100:.2f} м")
