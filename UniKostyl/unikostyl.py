import io, pygame, sys, time, ctypes, os, math
import numpy as np
from widgets import *
import cv2

# Idea 5: кластеризация кусков ворот + гистерезис потери (общий модуль
# с Pi — одни и те же семантики в локальном виде и в rpi_elp_cam.py).
import gate_cluster

try:
    # Idea 6: дистанция до ворот по размеру (d = a/(b-r) - c из
    # distance_calib.json; без калибровки — аналитический fallback).
    # Импорт не удался — readout живёт без дистанции (углы не трогаются).
    import distance as _distance
except Exception:
    _distance = None

try:
    # Idea 8: self-маска — вырезание корпуса робота (плата Pi, карбоновые
    # трубки) из поиска цвета. Как FisheyeProcessor: импорт не удался —
    # работаем без исключения зоны (старое поведение).
    import self_mask as _self_mask
except Exception:
    _self_mask = None

try:
    # Idea 2: панель статуса ворот/источника/публикации (отдельный модуль).
    # Как и FisheyeProcessor в cv_kostyl.py: если импорт не удался, UI
    # продолжает работать без панели.
    import status_panel as _status_panel
except Exception:
    _status_panel = None

try:
    # Idea 7: радар («кружочек») — включаемая панель в главном UI (кнопка
    # RADAR). Данные — те же источники, что у панели статуса: статус Pi
    # синхронизируется из её виджета (парсинг один), локальный трекинг —
    # тем же set_local_results. Импорт не удался — UI живёт без радара.
    import radar_panel as _radar_panel
except Exception:
    _radar_panel = None

try:
    # HSV-вид с масками выбранных ворот (без искажения плоскости).
    # Импорт не удался — UI живёт без этой кнопки/окна.
    import hsv_view as _hsv_view
except Exception:
    _hsv_view = None

try:
    # INFO: русская справка по всем органам управления (pygame overlay).
    import help_overlay as _help_overlay
except Exception:
    _help_overlay = None

# Idea 10/11: привязка к полю (field_map.json, кнопка BIND FIELD) + камерный
# советчик границы поля. field_map — чистый stdlib (как gate_store, импорт
# без guarded); field_boundary нужен cv2/numpy — как self_mask: импорт не
# удался, UI живёт без советчика границы (adviser, ничего не блокирует).
import field_map as _field_map

try:
    import field_boundary as _field_boundary
except Exception:
    _field_boundary = None


# --- ИНИЦИАЛИЗАЦИЯ C++ БИБЛИОТЕКИ (опционально) ---
class Thresholds_t(ctypes.Structure):
    _fields_ = [
        ("Lmin", ctypes.c_int),
        ("Lmax", ctypes.c_int),
        ("Amin", ctypes.c_int),
        ("Amax", ctypes.c_int),
        ("Bmin", ctypes.c_int),
        ("Bmax", ctypes.c_int),
    ]


try:
    noise_lib = ctypes.CDLL("lib/noise_filter/noise.dll")
    noise_lib.remove_noise.restype = Thresholds_t
    LAB_type = ((ctypes.c_bool * 256) * 256) * 101
except:
    noise_lib = None


# --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ОБРАБОТКИ ---
def rgb2lab(image):
    labpix = cv2.cvtColor(image, cv2.COLOR_RGB2LAB).astype(np.int16)
    labpix[:, :, 0] = labpix[:, :, 0] * 100 // 255
    labpix[:, :, 1] -= 128
    labpix[:, :, 2] -= 128
    return labpix.astype(np.int8)


def lab_to_rgb(l, a, b):
    l_cv = np.uint8(l * 255 / 100)
    a_cv = np.uint8(a + 128)
    b_cv = np.uint8(b + 128)
    lab_pixel = np.array([[[l_cv, a_cv, b_cv]]], dtype=np.uint8)
    rgb_pixel = cv2.cvtColor(lab_pixel, cv2.COLOR_LAB2RGB)
    return rgb_pixel[0][0]


def threshold_filter(thr, pixels):
    ind = np.all(pixels >= thr[::2], axis=2) * np.all(pixels <= thr[1::2], axis=2)
    res = (ind * 255).astype(np.uint8)
    return cv2.cvtColor(res, cv2.COLOR_GRAY2RGB)


def filter_L(thr, pixels):
    l = pixels[:, :, 0]
    res = np.zeros((l.shape[0], l.shape[1], 3), dtype=np.uint8)
    res[l < thr[0]] = [0, 0, 0]
    res[(l >= thr[0]) & (l <= thr[1])] = [255, 255, 255]
    res[l > thr[1]] = [100, 100, 100]
    return res


def filter_A(thr, pixels):
    a = pixels[:, :, 1]
    res = np.zeros((a.shape[0], a.shape[1], 3), dtype=np.uint8)
    res[a < thr[2]] = [0, 100, 0]
    res[(a >= thr[2]) & (a <= thr[3])] = [255, 255, 255]
    res[a > thr[3]] = [100, 0, 0]
    return res


def filter_B(thr, pixels):
    b = pixels[:, :, 2]
    res = np.zeros((b.shape[0], b.shape[1], 3), dtype=np.uint8)
    res[b < thr[4]] = [0, 0, 100]
    res[(b >= thr[4]) & (b <= thr[5])] = [255, 255, 255]
    res[b > thr[5]] = [80, 80, 0]
    return res


def selection_rect(temp_area, img_r, pixels):
    """Экранный прямоугольник выделения на img_src -> Rect в пикселях кадра.

    Виджет растягивает кадр в свой rect без сохранения пропорций, поэтому
    обратное отображение линейное: X виджета -> столбцы кадра (масштаб
    ширина_кадра/ширина_виджета), Y -> строки (высота_кадра/высота_виджета).
    Кадр хранится как (H, W): ось 0 — Y (высота), ось 1 — X (ширина).
    Правая/нижняя границы — исключительные, готовые для срезов numpy.
    Возвращает pygame.Rect(left=x1, top=y1) или None, если выделение пустое.
    """
    if pixels is None or getattr(pixels, "ndim", 0) != 3:
        return None
    if img_r is None or img_r.width <= 0 or img_r.height <= 0:
        return None
    iw, ih = pixels.shape[1], pixels.shape[0]
    sx = iw / img_r.width
    sy = ih / img_r.height
    x1, x2 = int(max(0, (temp_area.left - img_r.left) * sx)), int(
        min(iw, (temp_area.right - img_r.left) * sx)
    )
    y1, y2 = int(max(0, (temp_area.top - img_r.top) * sy)), int(
        min(ih, (temp_area.bottom - img_r.top) * sy)
    )
    if x2 <= x1 or y2 <= y1:
        return None
    return pygame.Rect(x1, y1, x2 - x1, y2 - y1)


def threshold_from_area(rect, pixels):
    # pixels — numpy (H, W, 3): строки = Y (rect.top..bottom), столбцы = X
    # (rect.left..right). Раньше оси были переставлены — выделялось то, что
    # стоит в кадре «поперёк» выделения, и порог ворот получался чужим.
    sub = pixels[rect.top : rect.bottom, rect.left : rect.right]
    if sub.size == 0:
        return np.array([0, 100, -128, 127, -128, 127])
    return np.array(
        [
            np.min(sub[:, :, 0]),
            np.max(sub[:, :, 0]),
            np.min(sub[:, :, 1]),
            np.max(sub[:, :, 1]),
            np.min(sub[:, :, 2]),
            np.max(sub[:, :, 2]),
        ],
        dtype=np.int16,
    )


def threshold_sum(thr1, thr2):
    return [
        min(thr1[0], thr2[0]),
        max(thr1[1], thr2[1]),
        min(thr1[2], thr2[2]),
        max(thr1[3], thr2[3]),
        min(thr1[4], thr2[4]),
        max(thr1[5], thr2[5]),
    ]


def threshold_diff(thr1, rect, pixels):
    if noise_lib is None:
        return threshold_from_area(rect, pixels)
    thr_struct = Thresholds_t(
        int(thr1[0]),
        int(thr1[1]),
        int(thr1[2]) + 128,
        int(thr1[3]) + 128,
        int(thr1[4]) + 128,
        int(thr1[5]) + 128,
    )
    colorspace = LAB_type()
    # Оси как в threshold_from_area: строки = Y, столбцы = X.
    sub_pixels = pixels[rect.top : rect.bottom, rect.left : rect.right]
    for row in sub_pixels:
        for p in row:
            l, a, b = map(int, p)
            if (
                l >= thr1[0]
                and l <= thr1[1]
                and a >= thr1[2]
                and a <= thr1[3]
                and b >= thr1[4]
                and b <= thr1[5]
            ):
                colorspace[l][a + 128][b + 128] = 1
    result = noise_lib.remove_noise(colorspace, thr_struct, 1)
    return [
        result.Lmin,
        result.Lmax,
        result.Amin - 128,
        result.Amax - 128,
        result.Bmin - 128,
        result.Bmax - 128,
    ]


def compute_track(pixels_LAB):
    """Объединённая маска всех отслеживаемых цветов + трекинг по контуру.

    Возвращает (combined, track_results, target_state, current_angle).
    Вызывается только при новом кадре или изменении порогов.
    """
    global _last_slot_masks
    current_angle = -1
    track_results = []
    target_state = {"found": False, "in_zone": False}
    h, w = pixels_LAB.shape[:2]
    center_x, center_y = w // 2, h // 2
    zone_radius = int(ZONE_RADIUS_FRAC * min(h, w))
    # Idea 5: буфер меток CC под текущий размер кадра (переиспользуем).
    global _track_labels
    if _track_labels.shape != (h, w):
        _track_labels = np.empty((h, w), dtype=np.int32)

    combined = np.zeros((h, w, 3), dtype=np.uint8)
    gray_masks = []
    # Idea 6: оценщики дистанции на слот (лениво; модель уже загружена).
    global _dist_est
    if _distance is not None and _dist_est is None:
        _dist_est = [
            _distance.GateDistEstimator(
                model=_DIST_MODEL, hold_s=DIST_HOLD_S,
                drop_frac=DIST_DROP_FRAC, fill_min=DIST_FILL_MIN,
                gate_width_cm=GATE_WIDTH_CM,
            )
            for _ in TRACK_SLOTS
        ]
    # Idea 8: keep-маска зоны поиска (255 = искать, 0 = корпус робота).
    # Строится один раз на смену размера/параметров; None в RING-режиме и
    # когда self_mask.py недоступен.
    keep = _self_keep_mask(w, h)
    for i, slot in enumerate(TRACK_SLOTS):
        thr = thresholds[slot]
        if not slot_configured(thr):
            # Слот не откалиброван — не рисуем и не ищем (иначе его
            # «весь кадр» забьёт остальные цвета).
            gray_masks.append(None)
            continue
        if process_mode == "Bitmap":
            m = threshold_filter(thr, pixels_LAB)
            gray_m = cv2.cvtColor(m, cv2.COLOR_RGB2GRAY)
        else:
            # У L/A/B «краска» не даёт чёрного вне диапазона (там цветные
            # оттенки), поэтому маску берём только по белым пикселям
            # (все каналы == 255), иначе весь кадр считался бы «объектом».
            if process_mode == "L":
                m = filter_L(thr, pixels_LAB)
            elif process_mode == "A":
                m = filter_A(thr, pixels_LAB)
            else:
                m = filter_B(thr, pixels_LAB)
            gray_m = np.all(m == 255, axis=2).astype(np.uint8) * 255
        # Idea 8: корпус исключён и из ПОИСКА (detect_gate ниже не увидит
        # кусочек на плате/трубках), и из покраски (зелёная плата Pi не
        # выглядит «найденными воротами» в маске). Один bitwise_and на слот.
        if keep is not None:
            cv2.bitwise_and(gray_m, keep, dst=gray_m)
        combined[gray_m > 0] = TRACK_COLORS[i]
        gray_masks.append(gray_m)

    # Центральные оси и фиксированная круговая зона (активация цели).
    cv2.line(combined, (center_x, 0), (center_x, h), (100, 100, 100), 1)
    cv2.line(combined, (0, center_y), (w, center_y), (100, 100, 100), 1)
    cv2.circle(combined, (center_x, center_y), zone_radius, (150, 150, 150), 2)

    # Второй проход: Idea 5 — куски каждого цвета сливаются в ОДИН объект
    # (зазор < merge_gap — «тело противника»), мелочь и далёкие чужие
    # объекты отбрасываются, гистерезис тот же, что на Pi (gate_cluster).
    # findContours/boundingRect больше не используются: статистика компонент
    # из connectedComponentsWithStats + моменты/взвешенный центроид.
    for i, slot in enumerate(TRACK_SLOTS):
        rec = {
            "slot": slot,
            "enabled": gray_masks[i] is not None,
            "found": False,
            "angle": -1,
            "cx": -1,
            "cy": -1,
            "area": 0.0,
            "w_px": 0,
            "r_px": float("inf"),
            "radius": 0.0,
            "in_zone": False,
            "dist_cm": None,
            "dist_reliable": False,
        }
        if gray_masks[i] is None:
            track_results.append(rec)
            continue
        cparams = _cluster_params(w, h)
        st = _track_stability[i]
        st.jump_px = cparams["max_stray_px"]
        # Смена цвета слота (порогов) сбрасывает гистерезис слота.
        thr_key = tuple(int(v) for v in thresholds[slot])
        if thr_key != _track_last_thr[i]:
            _track_last_thr[i] = thr_key
            st.reset()
            # Idea 6: новый цвет — прежняя дистанция/удержание смысла не имеют.
            if _dist_est is not None:
                _dist_est[i].reset()
        prev = st.held_xy()
        det = gate_cluster.detect_gate(
            gray_masks[i], cparams,
            prev[0] if prev else None, prev[1] if prev else None,
            labels=_track_labels, want_members=(fisheye_mode == 2),
        )
        pub = st.update(det)
        if pub is not None:
            cxf, cyf = pub["cx"], pub["cy"]
            cx, cy = int(cxf), int(cyf)
            if fisheye_mode == 2:
                # RING: круговое среднее угла по компонентам слитого объекта
                # (вес = площадь). Раньше считалось по точкам контура —
                # центроиды компонент дают тот же смысл (0° = верх полосы)
                # без findContours; x полосы -> theta по всей ширине кадра.
                sin_sum = 0.0
                cos_sum = 0.0
                two_pi = 2.0 * math.pi
                for mcx, _mcy, ma in pub.get("members", ()):
                    theta = mcx / max(w, 1) * two_pi
                    sin_sum += ma * math.sin(theta)
                    cos_sum += ma * math.cos(theta)
                if sin_sum != 0.0 or cos_sum != 0.0:
                    angle = int(-math.degrees(math.atan2(sin_sum, cos_sum))) % 360
                else:
                    angle = -1
            else:
                dx, dy = cxf - center_x, center_y - cyf
                angle = int(math.degrees(math.atan2(dy, dx)) % 360)
            r_px = math.hypot(cxf - center_x, cyf - center_y)
            rec.update(
                found=True,
                angle=angle,
                cx=cx,
                cy=cy,
                area=float(pub["area"]),
                w_px=int(pub["w"]),
                r_px=r_px,
                radius=float(pub["radius"]),
                in_zone=r_px <= zone_radius,
            )
            if pub.get("held"):
                # Значение удержано гистерезисом (маска пуста, потеря ещё
                # не объявлена) — панель/радар могут пометить как «est».
                rec["held"] = True
            # Idea 6: дистанция по размеру слитого объекта (radius — та же
            # мера, что у калибратора и на Pi). Оценщик применяет защиту
            # (деление/0..500/NaN/экстраполяция), эвристики перекрытия и
            # удержание DIST_HOLD_S секунд. В RING-режиме полоса —
            # развёртка 360°: размеры в ней не пропорциональны угловым,
            # дистанция честно не считается (как и раньше — «н/д»).
            if _dist_est is not None and fisheye_mode != 2:
                d_cm, d_ok = _dist_est[i].update(
                    pub["radius"], pub["w"], pub["h"], pub["area"],
                    cxf, cyf, bool(pub.get("held")), time.monotonic(),
                    frame_min_dim=min(h, w), frame_w=w,
                )
                rec["dist_cm"] = float(d_cm) if d_cm is not None else None
                rec["dist_reliable"] = bool(d_ok) if d_cm is not None else False
        track_results.append(rec)

    # Цель: выбранный слот. Считаем угол/расстояние только когда цель в зоне.
    target_rec = next(
        (r for r in track_results if r["slot"] == TRACK_SLOTS[target_slot]),
        None,
    )
    if target_rec and target_rec["found"]:
        if target_rec["in_zone"] and fisheye_mode != 2:
            # Линия цели на маске. Дистанция уже посчитана в rec (Idea 6:
            # калиброванная модель d = a/(b-r) - c или аналитический
            # fallback Z = GATE_WIDTH_CM * (w/π) / w_px — тот же вид, что
            # был здесь со старым расчётом, но ширина ворот вместо мяча);
            # readout ниже читает target_state["dist_cm"] как раньше.
            cv2.line(
                combined,
                (center_x, center_y),
                (target_rec["cx"], target_rec["cy"]),
                TRACK_COLORS[target_slot],
                2,
            )
        target_state = dict(target_rec)
    else:
        target_state = {"found": False, "in_zone": False}

    # Совместимость: угол цели, иначе угол выбранного в списке слота.
    if target_state.get("found"):
        current_angle = target_state["angle"]
    else:
        sel_rec = next(
            (r for r in track_results if r["slot"] == thr_index), None
        )
        if sel_rec and sel_rec["found"]:
            current_angle = sel_rec["angle"]

    _last_slot_masks = {
        slot: gray_masks[i].copy()
        for i, slot in enumerate(TRACK_SLOTS)
        if gray_masks[i] is not None
    }
    return combined, track_results, target_state, current_angle


# --- ПЕРЕМЕННЫЕ СОСТОЯНИЯ ---
pygame.init()
screen_w, screen_h = 1280, 720
screen = pygame.display.set_mode((screen_w, screen_h), pygame.RESIZABLE)
clock = pygame.time.Clock()

camera_input_text = "0"
input_active = False
requested_source = None
last_angle = -1

input_box_rect = pygame.Rect(0, 0, 0, 0)

# Заставка
current_dir = os.path.dirname(__file__)
image_path = os.path.join(current_dir, "svaga.jpg")
try:
    splash = pygame.image.load(image_path)
    splash = pygame.transform.scale(splash, (screen_w, screen_h))
    screen.blit(splash, (0, 0))
    pygame.display.flip()
except:
    screen.fill((100, 100, 100))
    pygame.display.flip()

thresholds = [[0, 100, -128, 127, -128, 127] for _ in range(5)]
thr_index = 0
process_mode = "Bitmap"
is_pause = False
fisheye_mode = 0
pixels_LAB = np.array([])

pending_gates = {}
log_messages = []

# Idea 2: последний разобранный статус Pi из multipart-части видео 5555
# (см. status_panel.parse_status_payload). Обновляет cv_kostyl.py.
pi_status = None

# --- ОТСЛЕЖИВАНИЕ НЕСКОЛЬКИХ ЦВЕТОВ ---
# Первые 3 слота порогов (Gate 1..3) отслеживаются одновременно, каждый со
# своим углом. Три не связаны с кнопками SAVE: слот = просто цвет.
TRACK_SLOTS = (0, 1, 2)
TRACK_COLORS = (
    (255, 255, 0),   # Gate 1 — жёлтый (RGB)
    (0, 128, 255),   # Gate 2 — синий (RGB)
    (255, 128, 0),   # Gate 3 — оранжевый (RGB)
)
ZONE_RADIUS_FRAC = 0.25      # радиус зоны вокруг центра, доля от min(w, h)
KNOWN_TARGET_WIDTH_CM = 4.2       # реальный диаметр мяча, см (42 мм)
TARGET_MIN_AREA = 300        # минимальная площадь контура, px^2
FOCAL_DIVISOR = math.pi      # равноугольная проекция: f = ширина / FOV(рад) = w / π
# Idea 6: дистанция до ворот по размеру. dist_cm в rec считается моделью
# d = a/(b-r) - c из distance_calib.json (калибратор out/dist_calibrate.py),
# без калибровки — аналитический fallback Z = W * (w/π) / w_px (тот же вид,
# что старый target-расчёт с FOCAL_DIVISOR), но W — ШИРИНА ВОРОТ из
# регламента (45-60 см), а не мяч: старое значение с KNOWN_TARGET_WIDTH_CM
# занижало дистанцию в ~10 раз. Гард/удержание при заслонении — в
# distance.GateDistEstimator (T секунд, см. DIST_HOLD_S).
GATE_WIDTH_CM = 45.0         # ширина ворот для fallback (параметр турнира)
DIST_HOLD_S = 2.0            # удержание последнего достоверного значения, с
DIST_DROP_FRAC = 0.6         # порог «внезапно уменьшился» (заслон противником)
DIST_FILL_MIN = 0.25         # мин. заполненность бокса area/(w*h)
frame_seq = 0                # счётчик кадров — растёт, если пришёл новый кадр
_track_cache_key = None      # ключ кэша объединённой маски/трекинга
_track_cache_data = None     # (combined, track_results, target_state, current_angle)
target_slot = 2              # слот цели по умолчанию (3-й = оранжевый)
track_results = []           # [{slot, found, angle, cx, cy, area, w_px, r_px}, ...]
target_state = {}            # {found, angle, r_px, w_px, dist_cm, in_zone}
TRACK_PRINT_INTERVAL = 1.0   # как часто печатать углы в консоль, с
track_print_t0 = 0.0
# Пороги из коробки — «весь диапазон». Слот с такими порогами считается
# не настроенным: его нельзя рисовать/отслеживать (иначе весь кадр пройдёт
# через маску и забьёт собой остальные цвета).
THRESHOLD_DEFAULT = (0, 100, -128, 127, -128, 127)


def slot_configured(t):
    return any(int(v) != d for v, d in zip(t, THRESHOLD_DEFAULT))


# --- Idea 5: кластеризация + гистерезис локального трекинга ---
# Те же умолчания, что на Pi (см. gate_cluster): None = масштаб от ширины
# кадра (merge_gap 2.5%, max_stray 10%). min_area_px — TARGET_MIN_AREA
# (прежний порог контура на ноутбуке). Менять — здесь, пересоздания
# трекера не требуется: параметры разрешаются на каждом кадре.
CLUSTER_PARAMS = {
    "merge_gap_px": None,
    "max_stray_px": None,
    "open_kernel": gate_cluster.OPEN_KERNEL,
    "lost_frames": gate_cluster.LOST_FRAMES,
    "confirm_frames": gate_cluster.CONFIRM_FRAMES,
}


def _cluster_params(w, h):
    return gate_cluster.resolve_params(
        w, h, min_area_px=TARGET_MIN_AREA, **CLUSTER_PARAMS
    )


# --- Idea 8: self-маска — вырезание корпуса робота из поиска цвета ---
# Доли кадра (как секция "self_mask" в camera_settings.json): при любом
# размере рабочего кадра круг строится от min(w, h) — переносится между
# Pi 640x360 и ноутбуком 720x1280 (портрет после поворота) без пересчёта.
# Это ОТДЕЛЬНАЯ сущность: ZONE_RADIUS_FRAC — зона вывода угла на экране,
# self-маска — исключение зоны корпуса из ПОИСКА цвета. В RING-режиме
# (fisheye_mode == 2) круглая маска не применяется: там геометрия другая
# (полоса 360°), корпус вырезается трекбаром «cut center %».
SELF_MASK = {
    "enabled": True,
    "cx": 0.5,    # центр круга, доля ШИРИНЫ кадра
    "cy": 0.5,    # доля ВЫСОТЫ
    "r": 0.22,    # внутренний радиус, доля min(w, h)
    "r2": 0.0,    # внешний радиус кольца (0 = сплошной круг)
}
_self_keep = None            # буфер 255=искать / 0=корпус (uint8)
_self_keep_key = None        # (w, h, параметры) последней сборки


def _self_mask_fractions():
    """Актуальные доли self-маски (SELF_MASK поверх настроек файла)."""
    return dict(SELF_MASK)


def set_self_mask_fractions(frac):
    """Живое обновление self-маски (хук для будущих трекбаров CAM SET /
    Idea 7 радара). frac — dict с ключами enabled/cx/cy/r/r2 (доли).
    Кэш keep-маски сбрасывается: пересоберётся на ближайшем кадре."""
    global SELF_MASK, _self_keep_key
    if _self_mask is None:
        return
    SELF_MASK = dict(frac)
    _self_keep_key = None


def load_self_mask_settings(path=None):
    """Прочитать секцию "self_mask" из camera_settings.json (Идея 1 пишет
    файл атомарно и сохраняет чужие секции). Файл/секции нет или секция
    битая — остаются значения по умолчанию. Никогда не поднимает исключений.
    Вызывается один раз при импорте модуля; возвращает предупреждение|None."""
    global SELF_MASK, _self_keep_key
    if _self_mask is None:
        return None
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "camera_settings.json")
    section, warn = _self_mask.load_section(path)
    if warn:
        print(f"[self_mask] {warn}")
    if section is not None:
        SELF_MASK = dict(section)
        _self_keep_key = None
        print(
            "[self_mask] секция camera_settings.json применена: "
            f"enabled={SELF_MASK.get('enabled')}, cx={SELF_MASK.get('cx')}, "
            f"cy={SELF_MASK.get('cy')}, r={SELF_MASK.get('r')}, "
            f"r2={SELF_MASK.get('r2')} (доли кадра)"
        )
    return warn


def _self_keep_mask(w, h):
    """Keep-маска зоны поиска для текущего кадра (None = не исключать).

    Строится ТОЛЬКО при смене размера/параметров; на каждый кадр остаётся
    один cv2.bitwise_and в compute_track. RING-режим возвращает None
    (корпус там вырезается «cut center %» — геометрия полосы другая)."""
    global _self_keep, _self_keep_key
    if _self_mask is None or fisheye_mode == 2:
        return None
    frac = _self_mask_fractions()
    key = (w, h, fisheye_mode,
           frac.get("enabled"), frac.get("cx"), frac.get("cy"),
           frac.get("r"), frac.get("r2"))
    if key != _self_keep_key:
        params = _self_mask.params_from_fractions(w, h, frac)
        if params["enabled"] and params["r"] > 0:
            _self_keep = _self_mask.build_keep_mask(params, w, h)
            if params["r"] >= 0.45 * min(w, h):
                print(
                    f"[self_mask] радиус {params['r']:.0f} px покрывает почти "
                    f"весь кадр {w}x{h} — проверьте cx/cy/r"
                )
        else:
            _self_keep = None
        _self_keep_key = key
    return _self_keep


load_self_mask_settings()


# Гистерезис на каждый слот ворот 1..3. ВАЖНО: счётчики «пустых кадров»
# двигаются при каждом пересчёте compute_track (новый кадр ИЛИ правка
# порогов) — на паузе/кэше состояние замирает, как и раньше.
_track_stability = [gate_cluster.GateStability() for _ in TRACK_SLOTS]
# Последние виденные пороги слота: смена ЦВЕТА сбрасывает гистерезис слота
# (паритет с rpi_elp_cam.update_gate_from_rgb — прежняя позиция ворот
# бессмысленна для нового цвета; дрейг слайдеров = перекалибровка).
_track_last_thr = [None, None, None]
# Переиспользуемый буфер меток component для CC (без аллокаций на кадр).
_track_labels = np.empty((0, 0), dtype=np.int32)
_last_slot_masks = {}


# --- Idea 6: дистанция до ворот по размеру (расчёт в compute_track) ---
# Модель читается из distance_calib.json РЯДОМ С МОДУЛЕМ один раз при
# импорте (UniKostyl/distance_calib.json; на Pi — /home/twox/). Битый файл
# -> None -> аналитический fallback (Z = GATE_WIDTH_CM * (w/π) / w_px).
# Оценщики на каждый слот создаются лениво в compute_track (модель уже
# загружена); при смене цвета слота (порогов) сбрасываются вместе с
# гистерезисом Idea 5. Смена масштаба кадра не требует пересоздания:
# модель масштабирует r по min(ширина, высота) сама.
_DIST_MODEL = None
if _distance is not None:
    _DIST_MODEL = _distance.load_model()
_dist_est = None


# --- Idea 10: привязка к полю (field_map.json, кнопка BIND FIELD) ---
# FIELD_MAP — валидированный dict файла (None = файла нет/битый). Пока
# привязки нет, всё работает как раньше (радар без «est»-меток). Загрузка
# при импорте = восстановление привязки после рестарта (PROMPT §6).
FIELD_MAP = None
BIND_POS_PRESETS = ("center", "own_goal", "manual")  # пресеты позиции робота
BIND_POS_LABELS = ("POS: CENTER", "POS: OWN GOAL", "POS: MANUAL")
bind_pos_idx = 0
BIND_HEADING_DEG = 0.0        # курс камеры к полю в момент привязки (API)
BIND_BTN_OK = (185, 255, 185)     # зелёная метка кнопки после удачной записи
BIND_BTN_FAIL = (255, 185, 185)   # красная метка после неудачи
# --- Idea 11: камерный советчик границы поля (adviser, ничего не блокирует) ---
BOUNDARY_RAW = {}            # сырая секция "boundary" из field_map.json
_edge_detector = None        # field_boundary.BoundaryDetector (None = недоступен)
edge_state = None            # последний результат process(): dict | None
_edge_any_prev = False       # для печати «граница близко» по фронту 0->1
_ring_edge_warned = False    # RING-предупреждение печатается один раз


def load_field_map_settings(path=None):
    """Прочитать field_map.json: привязка ворот (Идея 10) + секция
    "boundary" (пороги советчика границы, Идея 11).

    Файл/привязки нет или файл битый — работаем как раньше (FIELD_MAP =
    None, поведение unchanged), секция boundary — с умолчаниями. Повторный
    вызов (например, тестами) перечитывает файл и пересоздаёт детектор.
    Никогда не поднимает исключений."""
    global FIELD_MAP, BOUNDARY_RAW, _edge_detector
    if path is None:
        path = _field_map.default_path()
    FIELD_MAP = _field_map.load_map(path)
    if FIELD_MAP is None:
        print(
            "[field_map] привязка к полю отсутствует (файла нет или он "
            "повреждён) — RADAR без est-меток"
        )
    elif not FIELD_MAP.get("gates"):
        print(
            "[field_map] привязка ворот пуста (пороги границы поля "
            "загружены) — для привязки нажмите BIND FIELD"
        )
    else:
        print(f"[field_map] привязка загружена: {_field_map.binding_report(FIELD_MAP)}")
    BOUNDARY_RAW = dict(FIELD_MAP.get("boundary") or {}) if FIELD_MAP else {}
    if _field_boundary is not None:
        _edge_detector = _field_boundary.BoundaryDetector(BOUNDARY_RAW)
    return FIELD_MAP


load_field_map_settings()

# HSV MASK и INFO: создаются лениво, окно живёт независимо от главного UI.
_hsv_window = None
if _hsv_view is not None:
    _hsv_window = _hsv_view.HsvMaskWindow(enabled=False, window_size=(720, 1280))
_help = None
if _help_overlay is not None:
    _help = _help_overlay.HelpOverlay(screen_size=(screen_w, screen_h))


# --- ВСПОМОГАТЕЛЬНЫЙ ВИДЖЕТ ДЛЯ ПАНЕЛИ ЛОГОВ ---
class LogPanel(Widget):
    def __init__(self, screen, rect):
        super().__init__(screen, rect)
        self.text = "Angle: ---\n\n--- History ---"
        self.font = pygame.font.Font(None, 20)
        self.bg_color = (31, 38, 54)
        self.text_color = (232, 238, 247)
        self.border_color = (80, 96, 128)

    def set_text(self, text):
        self.text = text

    def draw(self):
        pygame.draw.rect(self.screen, self.bg_color, self.rect, border_radius=7)
        pygame.draw.rect(self.screen, self.border_color, self.rect, 1, border_radius=7)
        lines = self.text.split("\n")
        y = self.rect.y + 5
        for line in lines:
            if line.strip():
                surf = self.font.render(line, True, self.text_color)
                self.screen.blit(surf, (self.rect.x + 5, y))
            y += self.font.get_height() + 2


# --- УПРАВЛЕНИЕ ---
def set_proc_mode(mode):
    global process_mode
    process_mode = mode


def set_pause():
    global is_pause, widgets
    is_pause = not is_pause
    widgets["btn_pause"].label.text = ">" if is_pause else "||"


def toggle_radar():
    """Idea 7 (запрос пользователя): радар в ОТДЕЛЬНОМ ресайз-окне (cv2,
    WINDOW_NORMAL), а не в панели правой колонки. Данные — те же
    (_gate_data из radar_panel), ESC в окне закрывает радар."""
    radar = widgets.get("radar_panel")
    btn = widgets.get("btn_radar")
    if radar is None:
        return
    radar.toggle_window()
    if btn is not None and hasattr(btn, "label"):
        # Зелёная метка = окно радара открыто.
        btn.label.color = (185, 255, 185) if getattr(radar, "_win_open", False) else (255, 255, 255)


def toggle_hsv_view():
    """HSV MASK: отдельное cv2-окно с HSV-представлением кадра и маской
    выбранных ворот. Искажения плоскости нет: кадр и маска — один и тот же
    HxW, окно только letterbox-масштабируется."""
    if _hsv_window is None:
        return
    opened = _hsv_window.toggle()
    btn = widgets.get("btn_hsv")
    if btn is not None and hasattr(btn, "label"):
        btn.label.color = (185, 255, 185) if opened else (255, 255, 255)


def toggle_help(section_id=None):
    """INFO: русская справка по главному окну, CAM SET, радару и HSV MASK."""
    if _help is None:
        return
    _help.toggle(section_id)
    btn = widgets.get("btn_info")
    if btn is not None and hasattr(btn, "label"):
        btn.label.color = (185, 255, 185) if _help.visible else (255, 255, 255)


def cycle_target():
    """Циклически переключает слот-цель для зоны вокруг центра: G1 -> G2 -> G3.

    Заодно выбирает этот слот в списке порогов и загружает его в слайдеры,
    чтобы пользователь сразу выделил нужный цвет рамкой R и откалибровал его.
    """
    global target_slot, thr_index
    target_slot = (target_slot + 1) % len(TRACK_SLOTS)
    slot = TRACK_SLOTS[target_slot]
    thr_index = slot
    widgets["itemlist_thr"].chosen = slot
    set_thr_to_sliders()
    widgets["btn_target"].label.text = f"TARGET G{slot + 1}"
    print(
        f"[TARGET] Цель: Gate {slot + 1} — откалибруйте слот (рамка R), "
        f"затем SAVE G{slot + 1}"
    )


def save_gate(gate_id):
    """
    Сохраняет цвет ворот с номером gate_id (1..3).

    Первые три слота порогов зарезервированы под ворота:
      слот 1 -> ворота 1, слот 2 -> ворота 2, слот 3 -> ворота 3.
    В отличие от старого save_to_cam(), здесь можно независимо сохранить
    каждый цвет; на Raspberry Pi уйдет обновление только для этого номера.
    """
    global thresholds, pending_gates, last_angle
    slot = gate_id - 1
    t = thresholds[slot]
    l_mid = (t[0] + t[1]) / 2
    a_mid = (t[2] + t[3]) / 2
    b_mid = (t[4] + t[5]) / 2
    rgb = lab_to_rgb(l_mid, a_mid, b_mid)
    pending_gates[gate_id] = (int(rgb[0]), int(rgb[1]), int(rgb[2]))
    update_log_panel(last_angle, rgb_saved=pending_gates[gate_id], gate_id=gate_id)
    print(f"\n--- SAVE REPORT (Gate {gate_id}, Slot {slot+1}) ---")
    print(f"RGB: {pending_gates[gate_id]}")


def save_to_cam():
    # Оставлено для совместимости со старым OpenKostyl: сохраняет ворота 1.
    save_gate(1)


def change_source():
    global requested_source, camera_input_text
    val = camera_input_text.strip()
    if val.isdigit():
        requested_source = int(val)
    else:
        requested_source = val


def paste_text_from_clipboard():
    """Текст из буфера обмена для поля CONNECT (Ctrl+V / Ctrl+Insert).

    Управляющие символы и переводы строк отбрасываются, длина ограничена.
    Пустой/бинарный буфер — пустая строка, без исключений.
    """
    try:
        pygame.scrap.init()
    except Exception:
        pass
    try:
        raw = pygame.scrap.get(pygame.SCRAP_TEXT)
    except Exception:
        return ""
    if not raw:
        return ""
    if isinstance(raw, bytes):
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("utf-16le", errors="ignore").rstrip("\x00")
    else:
        text = str(raw)
    return "".join(ch for ch in text.replace("\x00", "") if ch.isprintable())[:256]


# --- Idea 10: привязка к полю (кнопки BIND FIELD / POS в верхнем ряду) ---
def cycle_robot_pos():
    """Циклит пресет позиции робота: center -> own_goal -> manual -> ...

    Кнопка POS (рядом с BIND FIELD) показывает текущий выбор. Пресет —
    часть привязки (field_map.json: robot_pos), «manual» = произвольная
    точка поля."""
    global bind_pos_idx
    bind_pos_idx = (bind_pos_idx + 1) % len(BIND_POS_PRESETS)
    btn = widgets.get("btn_pos")
    if btn is not None and hasattr(btn, "label"):
        btn.label.text = BIND_POS_LABELS[bind_pos_idx]
    print(f"[BIND] Пресет позиции робота: {BIND_POS_PRESETS[bind_pos_idx]}")


def set_bind_heading(deg):
    """Idea 10: курс камеры относительно поля в момент привязки (0..360).

    Программный API (тесты/скрипты; UI-редактирование курса отложено —
    без обратного канала Arduino->Pi курс ни на что не влияет, см.
    field_map.apply_heading). Возвращает применённое значение."""
    global BIND_HEADING_DEG
    try:
        BIND_HEADING_DEG = float(deg) % 360.0
    except (TypeError, ValueError):
        pass
    return BIND_HEADING_DEG


def _bind_source_snapshot():
    """Снимок ворот для привязки: свежий статус Pi, иначе локальный трекинг.

    Выбор источника — тот же, что у панели/радара: статус Pi считается
    свежим PI_STATUS_STALE_S секунд (2 с), протухший -> локальный трекинг.
    Возвращает (snapshot, source_tag)."""
    stale_s = getattr(_status_panel, "PI_STATUS_STALE_S", 2.0)
    sp = widgets.get("status_panel")
    if sp is not None and getattr(sp, "source_kind", "local") == "pi" and sp.pi_status:
        try:
            fresh = (time.monotonic() - float(sp.pi_status.get("ts", 0.0))) <= stale_s
        except (TypeError, ValueError):
            fresh = False
        if fresh:
            return _field_map.snapshot_from_pi_status(sp.pi_status), "pi"
    return _field_map.snapshot_from_results(track_results), "local"


def bind_field(path=None):
    """Idea 10: привязка к полю — записать «снимок поля» в field_map.json.

    Для каждого ИЗМЕРЕННОГО (found) ворота: угол + дистанция (живые recs
    локального трекинга или свежий статус Pi) + пресет позиции робота
    (кнопка POS) + курс камеры. Запись атомарная; секция "boundary"
    (Идея 11) и неизвестные ключи сохраняются. ПРИБЛИЗИТЕЛЬНАЯ привязка,
    НЕ SLAM: учёт поворота робота появится только с обратным каналом
    Arduino->Pi (Идея 12 — молча канал не изобретается, документированное
    ограничение). Пока ворота вне вида, радар показывает направления из
    этого снимка приглушёнными дугами с пометкой «est». path=None пишет
    в штатный field_map.json рядом с модулем."""
    global FIELD_MAP, bind_pos_idx
    if _field_map is None:  # pragma: no cover — импорт без guarded всегда ок
        print("[BIND] field_map недоступен")
        return False
    btn = widgets.get("btn_bind")
    btn_label_ok = btn is not None and hasattr(btn, "label")
    if btn_label_ok:
        btn.label.color = (255, 255, 255)  # сброс индикации прошлого нажатия
    preset = BIND_POS_PRESETS[bind_pos_idx]
    snap, src = _bind_source_snapshot()
    if not snap:
        print(
            "[BIND] нет измеренных ворот — привязка не записана. "
            "Настройте слоты G1..G3 (SAVE), дождитесь обнаружения и повторите."
        )
        if btn_label_ok:
            btn.label.color = BIND_BTN_FAIL
        return False
    data = _field_map.build_map_data(snap, preset, BIND_HEADING_DEG, prev=FIELD_MAP)
    if path is None:
        path = _field_map.default_path()
    ok = _field_map.save_map(path, data)
    if ok:
        FIELD_MAP = data
        print(f"[BIND] Поле привязано ({src}): {_field_map.binding_report(data)}")
        print(
            "[BIND] ворота, ушедшие из FOV, будут показываться на радаре "
            "с пометкой est (поворот робота НЕ учитывается — нет обратного "
            "канала, ограничение Идеи 10)"
        )
        log_messages.insert(
            0,
            f"[{time.strftime('%H:%M:%S')}] Field bound: {preset}, "
            f"{len(data.get('gates', {}))} gates",
        )
        if len(log_messages) > 10:
            log_messages.pop()
        if btn_label_ok:
            btn.label.color = BIND_BTN_OK
    else:
        if btn_label_ok:
            btn.label.color = BIND_BTN_FAIL
    return ok


def _estimated_directions():
    """Idea 10: «last known» направления из привязки ({gid: {...}}).

    Пусто, пока привязки нет — потребители (радар/лог) ведут себя как
    раньше. Вызов каждый кадр дешёвый (None/пустые gates -> {})."""
    if not FIELD_MAP:
        return {}
    return _field_map.estimated_directions(FIELD_MAP)


# --- Idea 2: данные для панели статуса (вызывает cv_kostyl.py) ---
def set_pi_status(status):
    """Принимает разобранный статус Pi (status_panel.parse_status_payload).

    status = None означает «статуса нет» (панель вернётся к локальному
    трекингу). Вызывается при получении multipart-части 2 видео 5555.
    """
    global pi_status
    pi_status = status
    if _status_panel is not None and "status_panel" in widgets:
        widgets["status_panel"].on_pi_status(status)


def set_video_source(kind, desc=""):
    """Индикация источника видео: kind 'local'|'pi', desc — '0'/IP Pi."""
    if _status_panel is not None and "status_panel" in widgets:
        widgets["status_panel"].on_video_source(kind, desc)


def set_publish_state(ok, note=""):
    """Статус PUB 5557: True — bind удался, False — упал (note — причина)."""
    if _status_panel is not None and "status_panel" in widgets:
        widgets["status_panel"].on_publish_state(ok, note)


def note_colors_sent():
    """Отметка момента отправки цветов в 5557 (для 'sent Ns' на панели)."""
    if _status_panel is not None and "status_panel" in widgets:
        widgets["status_panel"].on_colors_sent()


def update_log_panel(angle, rgb_saved=None, gate_id=None):
    global log_messages, widgets
    if track_results:
        parts = []
        for r in track_results:
            if not r.get("enabled", True):
                parts.append(f"G{r['slot'] + 1}=нс")
            elif r["found"]:
                parts.append(f"G{r['slot'] + 1}={r['angle']}°")
            else:
                parts.append(f"G{r['slot'] + 1}=—")
        angles_line = "Angles: " + " ".join(parts)
        unconfigured = [r["slot"] + 1 for r in track_results
                        if not r.get("enabled", True)]
        hint_line = (
            f"Слоты не настроены: {', '.join(map(str, unconfigured))} (R)"
            if unconfigured
            else None
        )
    else:
        angles_line = f"Angle: {angle}°" if angle != -1 else "Angle: ---"
        hint_line = None

    if target_state.get("found"):
        if target_state.get("in_zone"):
            if target_state.get("dist_cm") is not None:
                dist_m = target_state["dist_cm"] / 100.0
                dist_txt = f"~{dist_m:.2f} м"
                # Idea 6: dist_reliable=False — значение удержано после
                # заслонения/сомнительное; честно помечаем в readout.
                if not target_state.get("dist_reliable", False):
                    dist_txt += " (удержано)"
            else:
                dist_txt = "дистанция н/д (RING)"
            target_line = (
                f"Target G{target_slot + 1}: {target_state['angle']}°, "
                f"{dist_txt} (r={target_state['r_px']:.0f}px)"
            )
        else:
            target_line = (
                f"Target G{target_slot + 1}: вне зоны "
                f"(r={target_state['r_px']:.0f}px)"
            )
    else:
        target_rec = next(
            (r for r in track_results
             if r["slot"] == TRACK_SLOTS[target_slot]),
            None,
        )
        if target_rec is not None and not target_rec.get("enabled", True):
            target_line = f"Target G{target_slot + 1}: слот не настроен"
        else:
            target_line = f"Target G{target_slot + 1}: не найден"

    if rgb_saved is not None:
        timestamp = time.strftime("%H:%M:%S")
        prefix = f"G{gate_id} " if gate_id is not None else ""
        log_messages.insert(0, f"[{timestamp}] Saved {prefix}RGB: {rgb_saved}")
        if len(log_messages) > 10:
            log_messages.pop()
    history = "\n".join(log_messages) if log_messages else "No saves yet"
    header = angles_line + "\n" + target_line
    if hint_line:
        header += "\n" + hint_line
    log_text = header + "\n\n--- History ---\n" + history
    widgets["log_label"].set_text(log_text)


def set_thr_to_sliders():
    global thresholds, thr_index, widgets
    t = thresholds[thr_index]
    widgets["slider_L_low"].set_value(t[0])
    widgets["slider_L_high"].set_value(t[1])
    widgets["slider_A_low"].set_value(t[2])
    widgets["slider_A_high"].set_value(t[3])
    widgets["slider_B_low"].set_value(t[4])
    widgets["slider_B_high"].set_value(t[5])


# --- ВЕРСТКА ВИДЖЕТОВ ---
def update_all_widgets():
    global screen_w, screen_h, widgets, input_box_rect
    if screen_w < 100 or screen_h < 100:
        return

    margin = 30
    top_y = 14
    btn_h = 36

    def sync_btn(name, x, y, w, h):
        widgets[name].rect = pygame.Rect(x, y, w, h)
        if hasattr(widgets[name], "label"):
            widgets[name].label.rect = widgets[name].rect

    # 1. ТОП: одна строка, все действия основной панели помещаются без наложений.
    widgets["itemlist_select"].rect = pygame.Rect(margin, top_y, 150, btn_h)
    for i, item in enumerate(widgets["itemlist_select"].items):
        item.rect = pygame.Rect(margin + i * 50, top_y, 50, btn_h)

    mode_x = 190
    sync_btn("btn_bitmap", mode_x, top_y, 82, btn_h)
    sync_btn("btn_l", mode_x + 88, top_y, 36, btn_h)
    sync_btn("btn_a", mode_x + 128, top_y, 36, btn_h)
    sync_btn("btn_b", mode_x + 168, top_y, 36, btn_h)
    sync_btn("btn_pause", mode_x + 212, top_y, 48, btn_h)
    # Внешние модули добавляют свои кнопки в общий ряд.
    if "btn_cam_settings" in widgets:
        sync_btn("btn_cam_settings", 460, top_y, 105, btn_h)
    if "btn_radar" in widgets:
        sync_btn("btn_radar", 575, top_y, 78, btn_h)
    if "btn_bind" in widgets:
        sync_btn("btn_bind", 663, top_y, 112, btn_h)
    if "btn_pos" in widgets:
        sync_btn("btn_pos", 785, top_y, 120, btn_h)
    if "btn_hsv" in widgets:
        sync_btn("btn_hsv", 915, top_y, 92, btn_h)
    if "btn_info" in widgets:
        sync_btn("btn_info", 1017, top_y, 78, btn_h)

    # 2. КАМЕРЫ (СЛЕВА)
    v_y = top_y + btn_h + 20
    gap = 6
    max_v_w = (screen_w - (margin * 2) - 250) // 2
    max_v_h = int(screen_h * 0.65)
    side = min(max_v_w, max_v_h)

    x_left = margin
    x_right = x_left + side + gap
    widgets["img_src"].rect = pygame.Rect(x_left, v_y, side, side)
    widgets["img_proc"].rect = pygame.Rect(x_right, v_y, side, side)
    widgets["label_coords"].rect = pygame.Rect(x_left, v_y + side + 5, 0, 0)
    widgets["label_mode"].rect = pygame.Rect(x_right, v_y + side + 5, 0, 0)

    # 3. МЕНЮ КАМЕРЫ (СПРАВА)
    cam_x = x_right + side + 30
    widgets["label_cam_hint"].rect = pygame.Rect(cam_x, v_y, 200, 20)
    input_box_rect = pygame.Rect(cam_x, v_y + 30, 200, 35)
    sync_btn("btn_connect", cam_x, v_y + 75, 120, 35)

    # Панель логов – под полем ввода камеры
    log_y = v_y + 130
    widgets["log_label"].rect = pygame.Rect(cam_x, log_y, 250, 200)

    # Idea 2: панель статуса — под панелью логов в правой колонке.
    # При маленьком окне не вылезает за правый/нижний край.
    sp_x = sp_y = sp_h = None
    if "status_panel" in widgets:
        sp_x = max(margin, min(cam_x, screen_w - 252))
        sp_y = log_y + 210
        sp_h = max(80, min(145, screen_h - sp_y - 75))
        widgets["status_panel"].rect = pygame.Rect(sp_x, sp_y, 250, sp_h)

    # Idea 7: радар («кружочек») — в правой колонке ПОД панелью статуса
    # (ширина как у колонки; высота растёт с окном, при тесноте — минимум).
    if "radar_panel" in widgets:
        rp_x = sp_x if sp_x is not None else max(margin, min(cam_x, screen_w - 252))
        rp_y = (sp_y + sp_h + 6) if sp_y is not None else (log_y + 210)
        rp_h = max(96, min(260, screen_h - rp_y - 60))
        widgets["radar_panel"].rect = pygame.Rect(rp_x, rp_y, 250, rp_h)

    # 4. ТРЕШХОЛДЫ
    list_y = screen_h - 80
    col_text_w = 400
    for i, item in enumerate(widgets["itemlist_thr"].items):
        if i < 3:
            item.rect = pygame.Rect(margin, list_y + i * 20, col_text_w, 20)
        else:
            item.rect = pygame.Rect(
                margin + col_text_w + 30, list_y + (i - 3) * 20, col_text_w, 20
            )
    widgets["itemlist_thr"].rect = pygame.Rect(margin, list_y, col_text_w * 2 + 30, 60)

    # 5. СЛАЙДЕРЫ
    s_y = list_y - 50
    col_w = (screen_w - (margin * 2) - 100) // 3
    for i, char in enumerate(["L", "A", "B"]):
        x_start = margin + i * (col_w + 45)
        widgets[f"label_{char}"].rect = pygame.Rect(x_start, s_y - 25, 0, 0)
        for suffix, y_off in [("low", 0), ("high", 20)]:
            s_name = f"slider_{char}_{suffix}"
            bg_name = f"bg_slider_{char}_{suffix}"
            widgets[s_name].borders = (x_start, x_start + col_w)
            widgets[s_name].rect.y = s_y + y_off - 10
            widgets[s_name].rect.height = 20
            widgets[bg_name].rect = pygame.Rect(x_start, s_y + y_off - 10, col_w, 20)
            widgets[s_name].set_value(widgets[s_name].value)

    # Кнопки выбора цели и сохранения цветов ворот (в один ряд справа внизу).
    save_w, save_h, save_gap, target_w = 120, 40, 10, 150
    save_y = screen_h - save_h - 20
    x_g3 = screen_w - 20 - save_w
    x_g2 = x_g3 - save_w - save_gap
    x_g1 = x_g2 - save_w - save_gap
    x_target = x_g1 - target_w - save_gap
    sync_btn("btn_target", x_target, save_y, target_w, save_h)
    sync_btn("btn_save_g1", x_g1, save_y, save_w, save_h)
    sync_btn("btn_save_g2", x_g2, save_y, save_w, save_h)
    sync_btn("btn_save_g3", x_g3, save_y, save_w, save_h)


# --- ИНИЦИАЛИЗАЦИЯ ВИДЖЕТОВ ---
pixels_placeholder = np.zeros((240, 320, 3), dtype=np.uint8)
widgets = {
    "img_src": ImageNumpy(
        screen, pygame.Rect(0, 0, 0, 0), source=pixels_placeholder, select_area=True
    ),
    "img_proc": ImageNumpy(screen, pygame.Rect(0, 0, 0, 0), source=pixels_placeholder),
    "btn_bitmap": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "Bitmap",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 28),
            stratch=True,
        ),
        func=set_proc_mode,
        args=("Bitmap",),
    ),
    "btn_l": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "L",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 28),
            stratch=True,
        ),
        func=set_proc_mode,
        args=("L",),
    ),
    "btn_a": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "A",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 28),
            stratch=True,
        ),
        func=set_proc_mode,
        args=("A",),
    ),
    "btn_b": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "B",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 28),
            stratch=True,
        ),
        func=set_proc_mode,
        args=("B",),
    ),
    "log_label": LogPanel(screen, pygame.Rect(0, 0, 250, 200)),
    "bg_slider_L_low": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "bg_slider_L_high": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "bg_slider_A_low": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "bg_slider_A_high": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "bg_slider_B_low": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "bg_slider_B_high": Widget(screen, pygame.Rect(0, 0, 0, 0), block_click=False),
    "slider_L_low": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(0, 100)
    ),
    "slider_L_high": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(0, 100)
    ),
    "slider_A_low": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(-128, 127)
    ),
    "slider_A_high": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(-128, 127)
    ),
    "slider_B_low": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(-128, 127)
    ),
    "slider_B_high": HorizSlider(
        screen, pygame.Rect(0, 0, 12, 30), borders=(0, 100), values=(-128, 127)
    ),
    "label_L": Label(
        screen, pygame.Rect(0, 0, 0, 0), "L", font=pygame.font.Font(None, 35)
    ),
    "label_A": Label(
        screen, pygame.Rect(0, 0, 0, 0), "A", font=pygame.font.Font(None, 35)
    ),
    "label_B": Label(
        screen, pygame.Rect(0, 0, 0, 0), "B", font=pygame.font.Font(None, 35)
    ),
    "itemlist_thr": ItemList(
        screen,
        pygame.Rect(0, 0, 450, 130),
        items=[
            Label(
                screen,
                pygame.Rect(0, 0, 450, 25),
                f"Gate {i+1}" if i < 3 else f"Thr {i+1}",
                font=pygame.font.Font(None, 22),
                stratch=False,
            )
            for i in range(5)
        ],
        padding_y=25,
    ),
    "itemlist_select": ItemList(
        screen,
        pygame.Rect(0, 0, 160, 40),
        items=[
            Label(
                screen,
                pygame.Rect(0, 0, 40, 40),
                t,
                color=(255, 255, 255),
                font=pygame.font.Font(None, 28),
                stratch=True,
            )
            for t in ["R", "+", "-"]
        ],
        padding_x=50,
    ),
    "btn_save_g1": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "SAVE G1",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 30),
            stratch=True,
        ),
        func=save_gate,
        args=(1,),
    ),
    "btn_save_g2": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "SAVE G2",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 30),
            stratch=True,
        ),
        func=save_gate,
        args=(2,),
    ),
    "btn_save_g3": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "SAVE G3",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 30),
            stratch=True,
        ),
        func=save_gate,
        args=(3,),
    ),
    "btn_target": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            f"TARGET G{target_slot + 1}",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 26),
            stratch=True,
        ),
        func=cycle_target,
    ),
    "btn_pause": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "||",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 35),
            stratch=True,
        ),
        func=set_pause,
    ),
    "label_mode": Label(
        screen, pygame.Rect(0, 0, 0, 0), "Mode", font=pygame.font.Font(None, 25)
    ),
    "label_coords": Label(
        screen, pygame.Rect(0, 0, 0, 0), "Coords", font=pygame.font.Font(None, 20)
    ),
    "label_cam_hint": Label(
        screen,
        pygame.Rect(0, 0, 0, 0),
        "Camera Source:",
        font=pygame.font.Font(None, 25),
    ),
    "btn_connect": Button(
        screen,
        pygame.Rect(0, 0, 0, 0),
        label=Label(
            screen,
            pygame.Rect(0, 0, 0, 0),
            "CONNECT",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 22),
            stratch=True,
        ),
        func=change_source,
    ),
}

# --- Idea 2: панель статуса ворот (правая колонка, под панелью логов) ---
if _status_panel is not None:
    widgets["status_panel"] = _status_panel.StatusPanel(
        screen, pygame.Rect(0, 0, 250, 145), TRACK_COLORS
    )

# --- Idea 7: радар («кружочек») + кнопка RADAR в верхнем ряду ---
# По умолчанию панель скрыта; кнопка переключает (зелёная метка = включён).
if _radar_panel is not None:
    widgets["radar_panel"] = _radar_panel.RadarPanel(
        screen, pygame.Rect(0, 0, 250, 96), TRACK_COLORS
    )
    widgets["btn_radar"] = Button(
        screen,
        pygame.Rect(575, 14, 78, 36),
        label=Label(
            screen,
            pygame.Rect(575, 14, 78, 36),
            "RADAR",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 24),
            stratch=True,
        ),
        func=toggle_radar,
    )

# --- HSV MASK: cv2-окно с HSV + маской ---
if _hsv_window is not None:
    widgets["btn_hsv"] = Button(
        screen,
        pygame.Rect(915, 14, 92, 36),
        label=Label(
            screen,
            pygame.Rect(915, 14, 92, 36),
            "HSV MASK",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 20),
            stratch=True,
        ),
        func=toggle_hsv_view,
    )

# --- INFO: русская справка ---
if _help is not None:
    widgets["btn_info"] = Button(
        screen,
        pygame.Rect(1017, 14, 78, 36),
        label=Label(
            screen,
            pygame.Rect(1017, 14, 78, 36),
            "INFO",
            color=(255, 255, 255),
            font=pygame.font.Font(None, 24),
            stratch=True,
        ),
        func=toggle_help,
    )

# --- Idea 10: BIND FIELD («Привязать к полю») + пресет позиции робота ---
# Верхний ряд, правее RADAR (650..740). bind_field() пишет field_map.json
# атомарно (см. field_map.py); POS циклит пресет center/own_goal/manual.
widgets["btn_bind"] = Button(
    screen,
    pygame.Rect(750, 15, 110, 35),
    label=Label(
        screen,
        pygame.Rect(750, 15, 110, 35),
        "BIND FIELD",
        color=(255, 255, 255),
        font=pygame.font.Font(None, 22),
        stratch=True,
    ),
    func=bind_field,
)
widgets["btn_pos"] = Button(
    screen,
    pygame.Rect(870, 15, 130, 35),
    label=Label(
        screen,
        pygame.Rect(870, 15, 130, 35),
        BIND_POS_LABELS[bind_pos_idx],
        color=(255, 255, 255),
        font=pygame.font.Font(None, 20),
        stratch=True,
    ),
    func=cycle_robot_pos,
)

wnames = list(widgets.keys())


# --- ОСНОВНАЯ ФУНКЦИЯ, ВЫЗЫВАЕМАЯ ИЗВНЕ ---
def main_loop_frame(image_pixels: np.array):
    global screen_w, screen_h, screen, widgets, pixels_LAB, thr_index, process_mode
    global thresholds, is_pause, camera_input_text, input_active, pending_gates, last_angle
    global track_results, target_state, target_slot, track_print_t0
    global frame_seq, _track_cache_key, _track_cache_data
    global edge_state, _edge_any_prev, _ring_edge_warned

    is_real_frame = False

    # 1. ОБНОВЛЕНИЕ КАДРА
    if image_pixels is not None:
        is_real_frame = True
        if not is_pause:
            frame_seq += 1  # новый кадр -> пересчитать маски
            pixels_LAB = rgb2lab(image_pixels)
            v_w, v_h = widgets["img_src"].rect.width, widgets["img_src"].rect.height
            if v_w > 0 and v_h > 0:
                # рисуем зону на копии, оригинал не трогаем
                src_preview = cv2.resize(
                    image_pixels, (v_w, v_h), interpolation=cv2.INTER_LINEAR
                )
                zone_r = int(ZONE_RADIUS_FRAC * min(v_w, v_h))
                cv2.circle(
                    src_preview, (v_w // 2, v_h // 2), zone_r, (180, 180, 180), 1
                )
                widgets["img_src"].pixels = src_preview

    # 2. ОБРАБОТКА СОБЫТИЙ
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            pygame.quit()
            sys.exit()
        if event.type == pygame.VIDEORESIZE:
            screen_w, screen_h = event.w, event.h
            screen = pygame.display.set_mode((screen_w, screen_h), pygame.RESIZABLE)
            update_all_widgets()
            if _help is not None:
                _help.set_screen_size((screen_w, screen_h))

        # Модальное INFO-окно забирает события перед органами управления.
        if _help is not None and _help.visible and _help.handle_event(event):
            continue

        if event.type == pygame.MOUSEBUTTONDOWN:
            if input_box_rect.collidepoint(event.pos):
                input_active = True
            else:
                input_active = False
            for w in wnames[::-1]:
                if widgets[w].process_mousedown(event):
                    break

        if event.type == pygame.KEYDOWN and input_active:
            mods = pygame.key.get_mods()
            ctrl_held = bool(mods & (getattr(pygame, "KMOD_CTRL", 0) | getattr(pygame, "KMOD_META", 0)))
            if ctrl_held:
                if event.key in (getattr(pygame, "K_v", None), getattr(pygame, "K_INSERT", None)):
                    pasted = paste_text_from_clipboard()
                    if pasted:
                        camera_input_text += pasted
                continue
            if event.key == pygame.K_BACKSPACE:
                camera_input_text = camera_input_text[:-1]
            elif event.key == pygame.K_RETURN:
                change_source()
            elif event.unicode:
                camera_input_text += event.unicode

        if event.type == pygame.MOUSEBUTTONUP:
            src_w = widgets["img_src"]
            temp_area = src_w.selected_area
            had_press = src_w.first_press is not None
            for w in wnames[::-1]:
                widgets[w].process_mouseup(event)
            if had_press and temp_area and pixels_LAB.size > 0:
                final_r = selection_rect(temp_area, src_w.rect, pixels_LAB)
                if final_r is not None:
                    mode = widgets["itemlist_select"].chosen
                    if mode == 0:
                        thresholds[thr_index] = list(
                            map(int, threshold_from_area(final_r, pixels_LAB))
                        )
                    elif mode == 1:
                        thresholds[thr_index] = threshold_sum(
                            thresholds[thr_index],
                            list(map(int, threshold_from_area(final_r, pixels_LAB))),
                        )
                    elif mode == 2:
                        thresholds[thr_index] = threshold_diff(
                            thresholds[thr_index], final_r, pixels_LAB
                        )
                    set_thr_to_sliders()

        if event.type == pygame.MOUSEMOTION:
            for w in wnames[::-1]:
                widgets[w].process_mousemotion(event)

    # 3. ОБНОВЛЕНИЕ ВИДЖЕТОВ И ПОЛУЧЕНИЕ ТЕКУЩИХ ПОРОГОВ
    for w in wnames:
        widgets[w].update()
    t = thresholds[thr_index]
    t[0], t[1] = widgets["slider_L_low"].value, widgets["slider_L_high"].value
    t[2], t[3] = widgets["slider_A_low"].value, widgets["slider_A_high"].value
    t[4], t[5] = widgets["slider_B_low"].value, widgets["slider_B_high"].value

    # 4. ОТРИСОВКА МАСОК ВСЕХ ОТСЛЕЖИВАЕМЫХ ЦВЕТОВ + ЗОНА ОКОЛО ЦЕНТРА
    # Считаем только при новом кадре или изменении порогов/режима —
    # иначе UI тормозит: раньше это повторялось на каждом такте (60 раз/с).
    current_angle = -1
    track_results = []
    target_state = {"found": False, "in_zone": False}
    if pixels_LAB.size > 0:
        cache_key = (
            frame_seq,
            tuple(tuple(int(v) for v in thresholds[s]) for s in TRACK_SLOTS),
            process_mode,
            fisheye_mode,
        )
        if _track_cache_key == cache_key and _track_cache_data is not None:
            combined, track_results, target_state, current_angle = (
                _track_cache_data
            )
        else:
            combined, track_results, target_state, current_angle = (
                compute_track(pixels_LAB)
            )
            _track_cache_key = cache_key
            _track_cache_data = (
                combined,
                track_results,
                target_state,
                current_angle,
            )
        widgets["img_proc"].pixels = combined

    # Окно HSV MASK: кормим только открытым и только реальным кадром.
    if _hsv_window is not None and _hsv_window.is_open and image_pixels is not None:
        _hsv_window.set_gate_colors(
            {slot + 1: TRACK_COLORS[slot] for slot in TRACK_SLOTS}
        )
        _hsv_window.set_frame(image_pixels, _last_slot_masks)

    # 5. ЛОГИРОВАНИЕ УГЛОВ (раз в секунду, не на каждом кадре)
    # Idea 10: «last known» направления из привязки поля (пусто без неё —
    # считаем один раз за кадр, радар в 5c ест тот же результат).
    est_map = _estimated_directions()
    if is_real_frame and time.monotonic() - track_print_t0 >= TRACK_PRINT_INTERVAL:
        track_print_t0 = time.monotonic()
        parts = ["[TRACKING]"]
        for r in track_results:
            if not r.get("enabled", True):
                parts.append(f"G{r['slot'] + 1}=не настроен")
            elif r["found"]:
                # Idea 6: дистанция в посекундный лог («*» = удержано/недостоверное).
                d_txt = ""
                if r.get("dist_cm") is not None:
                    d_txt = f" d={r['dist_cm']:.0f}см" + (
                        "" if r.get("dist_reliable", False) else "*"
                    )
                parts.append(
                    f"G{r['slot'] + 1}={r['angle']}° r={r['r_px']:.0f}px{d_txt}"
                )
            else:
                # Idea 10: ворота вне вида, но есть привязка поля —
                # направление «last known» с пометкой est.
                est = est_map.get(r["slot"] + 1)
                if est:
                    parts.append(f"G{r['slot'] + 1}=est:{est['angle_deg']:.0f}°")
                else:
                    parts.append(f"G{r['slot'] + 1}=—")
        print(" ".join(parts))
        if target_state.get("found") and target_state.get("in_zone"):
            dist_txt = (
                f"{target_state['dist_cm'] / 100:.2f} м"
                if target_state.get("dist_cm") is not None
                else "н/д (fisheye RING)"
            )
            if target_state.get("dist_cm") is not None and not target_state.get(
                "dist_reliable", False
            ):
                dist_txt += " (удержано)"
            print(
                f"[TARGET] Gate {TRACK_SLOTS[target_slot] + 1} в зоне: "
                f"угол={target_state['angle']}°, r={target_state['r_px']:.0f}px, "
                f"ширина={target_state['w_px']}px, дистанция≈{dist_txt}"
            )

    # 5b. Idea 2: живые данные панели статуса (локальный трекинг + такт FPS).
    # Статус Pi приходит отдельно через set_pi_status() из cv_kostyl.py.
    if _status_panel is not None and "status_panel" in widgets:
        widgets["status_panel"].set_local_results(track_results)
        widgets["status_panel"].note_frame(is_real_frame)

    # 5c. Idea 7: живые данные радара — те же источники, что у панели
    # статуса (локальный трекинг — тот же фид; статус Pi/источник/роли
    # синхронизируются из её виджета — парсинг multipart-JSON один).
    # Скрытый радар эти сеттеры не рисует — цена копеечная.
    if _radar_panel is not None and "radar_panel" in widgets:
        _radar = widgets["radar_panel"]
        _radar.set_local_results(track_results)
        _radar.set_target_gate(TRACK_SLOTS[target_slot] + 1)
        # Зона корпуса на радаре = self-маска Идеи 8 (RING исключён — там
        # корпус режется «cut center %», см. _self_keep_mask).
        _radar.set_body_zone(
            bool(SELF_MASK.get("enabled")) and fisheye_mode != 2,
            float(SELF_MASK.get("r", 0.0)),
            float(SELF_MASK.get("r2", 0.0)),
        )
        if pixels_LAB.size > 0:
            _rh, _rw = pixels_LAB.shape[:2]
            _radar.set_frame_size(_rw, _rh)
        _sp = widgets.get("status_panel")
        if _sp is not None:
            _radar.sync_from_status_panel(_sp)
        # Idea 10: «last known» направления из привязки — радар показывает
        # est-дугу только для ворот, которых сейчас нет в кадре (слияние в
        # _gate_data); без привязки сеттер получает {} (старое поведение).
        _radar.set_estimated(est_map)
        # Idea 11: подтверждённые опасные сектора границы (советчик; []
        # пока детектор недоступен/выключен/на паузе — цена сеттера ~0).
        _radar.set_edge_sectors((edge_state or {}).get("flags") or [])

    # 5d. Idea 11: камерный советчик границы поля — зелёный ковёр/белая
    # разметка = «пол», доля «не-пола» в секторе -> «граница близко».
    # ТОЛЬКО СОВЕТ: field_boundary не содержит serial/zmq — ничего не
    # блокирует, EDGE на Arduino не вводится без одобрения пользователя
    # (правило §4 PROMPT_EN). Пороги — секция "boundary" field_map.json
    # (калибровать на реальном поле). В RING-режиме не считаем:
    # развёртка полосы, «сектора вокруг центра» не имеют смысла.
    if (
        _edge_detector is not None
        and is_real_frame
        and not is_pause
        and image_pixels is not None
    ):
        if fisheye_mode == 2:
            if not _ring_edge_warned:
                _ring_edge_warned = True
                print(
                    "[EDGE] RING-режим: детектор границы выключен "
                    "(геометрия полосы, совет недоступен)"
                )
        else:
            # Точка подключения платы фототранзисторов (Идея 12): когда
            # появится обратный канал Arduino->Pi, состояние платы
            # подставляется вторым аргументом process(frame, board_flags)
            # — ПРИОРИТЕТ У ПЛАТЫ (fuse: board OR camera). Сегодня None:
            # канала нет, камера работает автономным советчиком.
            new_state = _edge_detector.process(image_pixels)
            if new_state is not None:
                edge_state = new_state
                if new_state["any"] and not _edge_any_prev:
                    ks = " ".join(
                        str(k) for k, f in enumerate(new_state["flags"]) if f
                    )
                    print(
                        f"[EDGE] граница близко: сектора {ks} "
                        "(СОВЕТ — ничего не блокирует)"
                    )
                _edge_any_prev = new_state["any"]

    # 6. ОТРИСОВКА ИНТЕРФЕЙСА
    screen.fill((24, 30, 44))
    widgets["btn_target"].label.text = f"TARGET G{TRACK_SLOTS[target_slot] + 1}"
    widgets["label_L"].text = f"L [{t[0]}, {t[1]}]"
    widgets["label_A"].text = f"A [{t[2]}, {t[3]}]"
    widgets["label_B"].text = f"B [{t[4]}, {t[5]}]"
    for i in range(5):
        name = f"Gate {i+1}" if i < 3 else f"Thr {i+1}"
        widgets["itemlist_thr"][i].text = f"{name}: {list(map(int, thresholds[i]))}"
    if widgets["itemlist_thr"].chosen != thr_index:
        thr_index = widgets["itemlist_thr"].chosen
        set_thr_to_sliders()

    for name, widget in widgets.items():
        if name == "btn_hsv" and _hsv_window is not None:
            widget.label.color = (185, 255, 185) if _hsv_window.is_open else (245, 245, 245)
        elif name == "btn_info" and _help is not None:
            widget.label.color = (185, 255, 185) if _help.visible else (245, 245, 245)

    for w in wnames:
        widgets[w].draw()

    pygame.draw.rect(
        screen, (31, 38, 54) if input_active else (39, 47, 66), input_box_rect, border_radius=6
    )
    pygame.draw.rect(
        screen,
        (120, 208, 255) if input_active else (80, 96, 128),
        input_box_rect,
        2,
        border_radius=6,
    )
    txt_surf = pygame.font.Font(None, 26).render(camera_input_text, True, (235, 240, 246))
    screen.blit(txt_surf, (input_box_rect.x + 5, input_box_rect.y + 8))

    if widgets["img_src"].first_press and widgets["img_src"].selected_area:
        pygame.draw.rect(screen, (0, 255, 0), widgets["img_src"].selected_area, 2)

    if _hsv_window is not None:
        _hsv_window.pump()

    if _help is not None and _help.visible:
        _help.draw(screen)

    pygame.display.update()
    clock.tick(60)

    # Сохраняем угол для панели логов
    last_angle = current_angle
    update_log_panel(current_angle, rgb_saved=None)
    # Возвращаем обновления цветов ворот (только если были нажаты SAVE G1..G3).
    # Формат: {1: (r,g,b), 2: (r,g,b), 3: (r,g,b)}.
    gates_to_send = dict(pending_gates)
    pending_gates = {}

    return (current_angle, gates_to_send)


# --- ЗАВЕРШЕНИЕ ИНИЦИАЛИЗАЦИИ ---
set_thr_to_sliders()
update_all_widgets()
