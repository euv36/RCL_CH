#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Развертка кругового изображения с конического/многогранного зеркала,
подавление бликов и заливка желтых ворот единым цветом.

Примеры запуска:
    # Фото + автоматическое определение центра/радиуса зеркала
    python3 UniKostyl/mirror_unwrap.py --input frame.jpg --output out

    # Явно задать центр, радиус и сектора ворот (углы в градусах)
    python3 UniKostyl/mirror_unwrap.py --input frame.jpg --output out \
        --center 640,360 --radius 350 --sectors 162,288 --sector-half-width 25

    # Взять кадр с камеры (индекс 0) и показать результат на экране
    python3 UniKostyl/mirror_unwrap.py --input 0 --output out --show
"""

import argparse
import os
import sys

import cv2
import numpy as np


# ============================================================================
# 1. ОПРЕДЕЛЕНИЕ ГЕОМЕТРИИ ЗЕРКАЛА
# ============================================================================

def detect_mirror_circle(image):
    """
    Пытается автоматически найти центр и радиус круглого зеркала.

    Используются два независимых способа:
      1) HoughCircles — хорошо работает, если зеркало имеет четкую границу;
      2) порог по яркости + минимальная описывающая окружность — запасной
         вариант, если круг разорван бликами или краем кадра.

    Возвращает (cx, cy, radius) либо None, если ничего не найдено.
    """
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    gray = cv2.GaussianBlur(gray, (9, 9), 2)

    h, w = gray.shape[:2]
    min_r = int(min(h, w) * 0.20)
    max_r = int(min(h, w) * 0.55)

    # --- Попытка 1: преобразование Хафа для окружностей ---
    circles = cv2.HoughCircles(
        gray,
        cv2.HOUGH_GRADIENT,
        dp=1.2,
        minDist=min(h, w) * 0.5,
        param1=100,
        param2=40,
        minRadius=min_r,
        maxRadius=max_r,
    )
    if circles is not None:
        circles = np.round(circles[0]).astype(int)
        # Выбираем окружность, ближайшую к центру кадра
        cx0, cy0 = w / 2.0, h / 2.0
        best = min(
            circles,
            key=lambda c: (c[0] - cx0) ** 2 + (c[1] - cy0) ** 2,
        )
        return int(best[0]), int(best[1]), int(best[2])

    # --- Попытка 2: яркая область зеркала + minEnclosingCircle ---
    _, thresh = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    kernel = np.ones((9, 9), np.uint8)
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel, iterations=2)
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, kernel, iterations=1)

    contours, _ = cv2.findContours(
        thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )
    if not contours:
        return None

    largest = max(contours, key=cv2.contourArea)
    if cv2.contourArea(largest) < 0.05 * w * h:
        return None

    (cx, cy), radius = cv2.minEnclosingCircle(largest)
    return int(cx), int(cy), int(radius)


def resolve_geometry(image, center_arg, radius_arg, inner_ratio, outer_ratio):
    """
    Разрешает параметры центра/радиуса с учетом аргументов командной строки.

    center_arg: "auto" или "x,y"
    radius_arg: "auto" или число пикселей (внешний радиус)
    """
    detected = None
    if center_arg.lower() == "auto" or radius_arg.lower() == "auto":
        detected = detect_mirror_circle(image)

    h, w = image.shape[:2]

    # --- Центр ---
    if center_arg.lower() == "auto":
        if detected is not None:
            cx, cy = detected[0], detected[1]
        else:
            # Запасной вариант: геометрический центр кадра
            cx, cy = w // 2, h // 2
    else:
        cx, cy = [int(v) for v in center_arg.split(",")]

    # --- Внешний радиус ---
    if radius_arg.lower() == "auto":
        if detected is not None:
            outer_r = float(detected[2])
        else:
            outer_r = 0.5 * min(h, w)
    else:
        outer_r = float(radius_arg)

    # Немного отступаем от края, чтобы не захватить фон за зеркалом
    outer_r = min(outer_r, 0.5 * min(h, w)) * outer_ratio
    inner_r = outer_r * inner_ratio
    return (int(round(cx)), int(round(cy))), inner_r, outer_r


# ============================================================================
# 2. РАЗВЕРТКА (UNWRAPPING / STRETCHING)
# ============================================================================

def unwrap_annulus(image, center, inner_r, outer_r,
                   angle_px=None, radial_px=None):
    """
    Полная полярная развертка кольца (без мертвой зоны в центре).

    cv2.warpPolar строит изображение, где:
        ось X результата = радиус,
        ось Y результата = угол.
    Затем cv2.transpose переставляет оси, чтобы получилась привычная
    панорама: по горизонтали — угол (0..360), по вертикали — радиус.

    inner_r/outer_r задают вырезаемое кольцо: центральная мертвая зона
    (крепление робота) отбрасывается, а внешняя часть растягивается
    на всю высоту панорамы.
    """
    if outer_r <= inner_r:
        raise ValueError("Внешний радиус должен быть больше внутреннего")

    # Разрешение панорамы. По умолчанию угловое разрешение примерно
    # соответствует длине внешней окружности: 2*pi*R пикселей.
    if radial_px is None:
        radial_px = max(1, int(round(outer_r)))
    if angle_px is None:
        angle_px = max(360, int(round(2.0 * np.pi * outer_r)))

    # Ограничиваем размеры, чтобы не съесть всю память на слабом Raspberry Pi.
    radial_px = int(np.clip(radial_px, 1, 4096))
    angle_px = int(np.clip(angle_px, 36, 8192))

    polar = cv2.warpPolar(
        image,
        dsize=(radial_px, angle_px),
        center=center,
        maxRadius=outer_r,
        flags=cv2.WARP_POLAR_LINEAR + cv2.WARP_FILL_OUTLIERS,
    )

    # polar имеет форму (angle_px, radial_px); транспонируем в панораму.
    panorama = cv2.transpose(polar)

    # Отрезаем центральную мертвую зону (строки, соответствующие r < inner_r).
    row_inner = int(round(inner_r / outer_r * radial_px))
    row_inner = int(np.clip(row_inner, 0, panorama.shape[0] - 1))
    panorama = panorama[row_inner:, :]

    return panorama


def unwrap_sector(image, center, inner_r, outer_r,
                  center_angle_deg, half_width_deg,
                  out_w=900, out_h=260):
    """
    Вырезает один радиальный сектор и растягивает его в прямоугольник.

    Это отдельная "лупа" для ворот: угол center_angle_deg находится
    в центре картинки, half_width_deg задает половину углового обзора.
    По горизонтали — угол, по вертикали — радиус (от inner_r к outer_r).

    В отличие от полной панорамы, здесь можно независимо задать
    out_w/out_h, то есть искусственно растянуть ворота сильнее.
    """
    a0 = np.deg2rad(center_angle_deg - half_width_deg)
    a1 = np.deg2rad(center_angle_deg + half_width_deg)

    # Сетка выходного изображения: u — угол, v — радиус.
    u = np.linspace(0.0, 1.0, out_w, dtype=np.float32)
    v = np.linspace(0.0, 1.0, out_h, dtype=np.float32)
    angles = a0 + (a1 - a0) * u
    radii = inner_r + (outer_r - inner_r) * v
    angle_grid, radius_grid = np.meshgrid(angles, radii)

    # Переводим полярные координаты обратно в пиксели исходного кадра.
    # Угол отсчитывается от оси +X по часовой стрелке (ось Y вниз),
    # что совпадает с соглашением warpPolar.
    map_x = center[0] + radius_grid * np.cos(angle_grid)
    map_y = center[1] + radius_grid * np.sin(angle_grid)

    # cv2.remap делает билинейную интерполяцию — ворота остаются гладкими.
    sector = cv2.remap(
        image,
        map_x.astype(np.float32),
        map_y.astype(np.float32),
        interpolation=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(0, 0, 0),
    )
    return sector


# ============================================================================
# 3. ВЫДЕЛЕНИЕ ВОРОТ, ПОДАВЛЕНИЕ БЛИКОВ И ЗАЛИВКА
# ============================================================================

def build_gate_mask(bgr,
                    h_min=15, h_max=40,
                    s_min=70, v_min=60,
                    glare_s_max=60, glare_v_min=235,
                    bridge_px=21,
                    min_area=None):
    """
    Строит сплошную маску желтых ворот.

    Порядок обработки:
      1) HSV-порог по желтому цвету;
      2) удаление бликов: белые пиксели имеют низкую S и высокую V;
      3) морфологическое открытие/закрытие для удаления шума и bridging;
      4) дилатация + cv2.convexHull: разрывы от бликов заливаются,
         а вогнутости контура выпрямляются;
      5) cv2.fillConvexPoly делает ворота монолитными.

    Возвращает (raw_mask, solid_mask).
    """
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)

    # --- Шаг 1: желтый цвет ---
    lower = np.array([h_min, s_min, v_min], dtype=np.uint8)
    upper = np.array([h_max, 255, 255], dtype=np.uint8)
    raw = cv2.inRange(hsv, lower, upper)

    # --- Шаг 2: подавление бликов ---
    # Блик — это почти белый пиксель: насыщенность низкая, яркость высокая.
    # Такие пиксели вырезаем из маски, чтобы они не разрывали ворота.
    glare = cv2.inRange(
        hsv,
        np.array([0, 0, glare_v_min], dtype=np.uint8),
        np.array([179, glare_s_max, 255], dtype=np.uint8),
    )
    raw[glare > 0] = 0

    # --- Шаг 3: мелкая морфология ---
    # Убираем одиночные пиксели/шум, затем закрываем небольшие щели.
    small_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    raw = cv2.morphologyEx(raw, cv2.MORPH_OPEN, small_kernel, iterations=1)

    if bridge_px > 0:
        bridge_kernel = cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (bridge_px, bridge_px)
        )
        raw = cv2.morphologyEx(raw, cv2.MORPH_CLOSE, bridge_kernel, iterations=1)

    # --- Шаг 4: объединение фрагментов, разорванных бликами ---
    # Дилатация "склеивает" куски в один компонент, после чего выпуклая
    # оболочка заливает промежуток между ними.
    if bridge_px > 0:
        dilate_kernel = cv2.getStructuringElement(
            cv2.MORPH_ELLIPSE, (bridge_px, bridge_px)
        )
        dilated = cv2.dilate(raw, dilate_kernel, iterations=1)
    else:
        dilated = raw

    num, labels, stats, _ = cv2.connectedComponentsWithStats(dilated, connectivity=8)
    solid = np.zeros_like(raw)

    if min_area is None:
        # Автоматический минимальный размер: отсекаем мелкий цветной мусор.
        min_area = max(40, int(0.00005 * raw.shape[0] * raw.shape[1]))

    for i in range(1, num):
        if stats[i, cv2.CC_STAT_AREA] < min_area:
            continue

        # Берем только настоящие желтые пиксели этого компонента (без
        # искусственно добавленной дилатации), затем строим выпуклую оболочку.
        ys, xs = np.where((labels == i) & (raw > 0))
        if len(xs) < 10:
            continue

        points = np.column_stack((xs, ys)).astype(np.int32)
        hull = cv2.convexHull(points)
        cv2.fillConvexPoly(solid, hull, 255)

    # --- Шаг 5: финальная заливка внутренних дырок ---
    # Внешние контуры заливаются целиком, поэтому дырки от бликов исчезают.
    contours, _ = cv2.findContours(
        solid, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )
    solid = np.zeros_like(raw)
    for cnt in contours:
        if cv2.contourArea(cnt) < min_area:
            continue
        cv2.drawContours(solid, [cnt], -1, 255, thickness=-1)

    return raw, solid


def render_outputs(bgr, solid_mask):
    """
    Готовит три картинки:
      - clean: только ворота чистого желтого цвета на черном фоне;
      - overlay: исходная развертка, где ворота залиты чистым желтым;
      - outlined: развертка с контуром ворот.
    """
    # Чистый желтый в BGR (не путать с RGB).
    pure_yellow = (0, 255, 255)

    clean = np.zeros_like(bgr)
    clean[solid_mask > 0] = pure_yellow

    overlay = bgr.copy()
    overlay[solid_mask > 0] = pure_yellow

    outlined = bgr.copy()
    contours, _ = cv2.findContours(
        solid_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )
    cv2.drawContours(outlined, contours, -1, (0, 255, 0), 2)

    return clean, overlay, outlined


def process_image(image, center, inner_r, outer_r, sector_angles,
                  sector_half_width, bridge_px, min_area,
                  sector_w=900, sector_h=260,
                  angle_px=None, radial_px=None):
    """
    Полный конвейер обработки одного кадра.
    Возвращает словарь с результатами (панорама, сектора, маски).
    """
    results = {}

    # --- Полная панорама кольца ---
    panorama = unwrap_annulus(
        image, center, inner_r, outer_r,
        angle_px=angle_px, radial_px=radial_px,
    )

    # Выделяем ворота на панораме.
    pano_raw, pano_solid = build_gate_mask(
        panorama, bridge_px=bridge_px, min_area=min_area
    )
    pano_clean, pano_overlay, pano_outlined = render_outputs(panorama, pano_solid)

    results["panorama"] = panorama
    results["panorama_raw_mask"] = pano_raw
    results["panorama_solid_mask"] = pano_solid
    results["panorama_clean"] = pano_clean
    results["panorama_overlay"] = pano_overlay
    results["panorama_outlined"] = pano_outlined

    # --- Отдельные растянутые сектора для каждого угла ворот ---
    results["sectors"] = []
    for angle in sector_angles:
        sector = unwrap_sector(
            image, center, inner_r, outer_r,
            center_angle_deg=angle,
            half_width_deg=sector_half_width,
            out_w=sector_w, out_h=sector_h,
        )
        sec_raw, sec_solid = build_gate_mask(
            sector, bridge_px=bridge_px, min_area=min_area
        )
        sec_clean, sec_overlay, sec_outlined = render_outputs(sector, sec_solid)
        results["sectors"].append(
            {
                "angle": angle,
                "raw": sector,
                "raw_mask": sec_raw,
                "solid_mask": sec_solid,
                "clean": sec_clean,
                "overlay": sec_overlay,
                "outlined": sec_outlined,
            }
        )

    return results


# ============================================================================
# 4. ВВОД/ВЫВОД И CLI
# ============================================================================

def read_input(source):
    """Читает изображение из файла или с камеры (если source — число)."""
    if source.isdigit():
        cap = cv2.VideoCapture(int(source))
        if not cap.isOpened():
            raise RuntimeError(f"Не удалось открыть камеру {source}")
        ok, frame = cap.read()
        cap.release()
        if not ok:
            raise RuntimeError("Камера не вернула кадр")
        return frame

    image = cv2.imread(source, cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"Не удалось прочитать изображение: {source}")
    return image


def save_results(results, out_dir):
    """Сохраняет все результаты в out_dir."""
    os.makedirs(out_dir, exist_ok=True)

    def save(name, img):
        path = os.path.join(out_dir, name)
        cv2.imwrite(path, img)
        print(f"[OUT] {path}")

    save("panorama.jpg", results["panorama"])
    save("panorama_mask.png", results["panorama_solid_mask"])
    save("panorama_clean.jpg", results["panorama_clean"])
    save("panorama_overlay.jpg", results["panorama_overlay"])
    save("panorama_outlined.jpg", results["panorama_outlined"])

    for sec in results["sectors"]:
        angle = sec["angle"]
        save(f"gate_{angle}_raw.jpg", sec["raw"])
        save(f"gate_{angle}_mask.png", sec["solid_mask"])
        save(f"gate_{angle}_clean.jpg", sec["clean"])
        save(f"gate_{angle}_overlay.jpg", sec["overlay"])
        save(f"gate_{angle}_outlined.jpg", sec["outlined"])


def show_results(results):
    """Показывает результат в окнах OpenCV. Выход — ESC или Q."""
    windows = [
        ("Panorama", results["panorama"]),
        ("Gates solid", results["panorama_clean"]),
        ("Gates overlay", results["panorama_overlay"]),
    ]
    for sec in results["sectors"]:
        windows.append((f"Gate {sec['angle']} raw", sec["raw"]))
        windows.append((f"Gate {sec['angle']} solid", sec["clean"]))

    for name, img in windows:
        cv2.imshow(name, img)

    print("[INFO] Нажмите ESC или Q для выхода из окон")
    while True:
        key = cv2.waitKey(50) & 0xFF
        if key in (27, ord("q")):
            break
    cv2.destroyAllWindows()


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Развертка кругового зеркала, подавление бликов и заливка ворот"
    )
    parser.add_argument(
        "--input", default="0",
        help="Путь к фото или индекс камеры (по умолчанию: 0)",
    )
    parser.add_argument(
        "--output", default="out",
        help="Папка для сохранения результатов (по умолчанию: out)",
    )
    parser.add_argument(
        "--center", default="auto",
        help="Центр зеркала: 'auto' или 'x,y' (по умолчанию: auto)",
    )
    parser.add_argument(
        "--radius", default="auto",
        help="Внешний радиус зеркала в пикселях или 'auto'",
    )
    parser.add_argument(
        "--inner-ratio", type=float, default=0.20,
        help="Внутренний радиус как доля внешнего (мертвая зона), по умолчанию 0.20",
    )
    parser.add_argument(
        "--outer-ratio", type=float, default=0.98,
        help="Доля от найденного радиуса, которая считается зеркалом, по умолчанию 0.98",
    )
    parser.add_argument(
        "--sectors", default="162,288",
        help="Углы центров ворот через запятую (по умолчанию: 162,288)",
    )
    parser.add_argument(
        "--sector-half-width", type=float, default=25.0,
        help="Половина углового обзора сектора в градусах (по умолчанию: 25)",
    )
    parser.add_argument(
        "--sector-width", type=int, default=900,
        help="Ширина растянутого сектора (по умолчанию: 900)",
    )
    parser.add_argument(
        "--sector-height", type=int, default=260,
        help="Высота растянутого сектора (по умолчанию: 260)",
    )
    parser.add_argument(
        "--angle-px", type=int, default=0,
        help="Ширина панорамы в пикселях (0 = авто по длине окружности)",
    )
    parser.add_argument(
        "--radial-px", type=int, default=0,
        help="Радиальное разрешение панорамы (0 = авто)",
    )
    parser.add_argument(
        "--bridge-px", type=int, default=21,
        help="Размер ядра для склейки разрывов от бликов (по умолчанию: 21)",
    )
    parser.add_argument(
        "--min-area", type=int, default=0,
        help="Минимальная площадь ворот в пикселях (0 = авто)",
    )
    parser.add_argument(
        "--h-min", type=int, default=15, help="Нижняя граница Hue для желтого",
    )
    parser.add_argument(
        "--h-max", type=int, default=40, help="Верхняя граница Hue для желтого",
    )
    parser.add_argument(
        "--s-min", type=int, default=70, help="Минимальная насыщенность ворот",
    )
    parser.add_argument(
        "--v-min", type=int, default=60, help="Минимальная яркость ворот",
    )
    parser.add_argument(
        "--glare-s-max", type=int, default=60,
        help="Максимальная насыщенность блика",
    )
    parser.add_argument(
        "--glare-v-min", type=int, default=235,
        help="Минимальная яркость блика",
    )
    parser.add_argument(
        "--show", action="store_true", help="Показать результат в окнах OpenCV",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    image = read_input(args.input)
    print(f"[INFO] Вход: {args.input}, размер {image.shape[1]}x{image.shape[0]}")

    center, inner_r, outer_r = resolve_geometry(
        image, args.center, args.radius,
        inner_ratio=args.inner_ratio,
        outer_ratio=args.outer_ratio,
    )
    print(f"[INFO] Центр: {center}, внутренний радиус: {inner_r:.1f}, "
          f"внешний радиус: {outer_r:.1f}")

    sector_angles = [
        float(v.strip()) for v in args.sectors.split(",") if v.strip()
    ]

    # min_area применяется и к панораме, и к секторам. Для секторов площадь
    # считается в их собственном масштабе, поэтому 0 = авто в каждой картинке.
    min_area = args.min_area if args.min_area > 0 else None

    results = process_image(
        image,
        center=center,
        inner_r=inner_r,
        outer_r=outer_r,
        sector_angles=sector_angles,
        sector_half_width=args.sector_half_width,
        bridge_px=args.bridge_px,
        min_area=min_area,
        sector_w=args.sector_width,
        sector_h=args.sector_height,
        angle_px=(args.angle_px if args.angle_px > 0 else None),
        radial_px=(args.radial_px if args.radial_px > 0 else None),
    )

    save_results(results, args.output)

    if args.show:
        show_results(results)

    return 0


if __name__ == "__main__":
    sys.exit(main())
