#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rpi_elp_cam.py — оптимизированный захват ELP 180° USB-камеры на Raspberry Pi 4.

Почему так:
  * ELP-камера отдаёт полный угол обзора 180° только в режиме 1920x1080.
    Режимы 1280x720 / 1024x768 / 640x480 — это центральный кроп, а не
    уменьшенный полный кадр. Поэтому камера всегда захватывает 1920x1080,
    а уменьшение делается программно.
  * Захват идёт в MJPG (YUYV на 1080p даёт всего ~6 fps).
  * Из OpenCV кадр забирается как сырой JPEG (CAP_PROP_CONVERT_RGB=0) и
    декодируется сразу в уменьшенном масштабе (IMREAD_REDUCED_COLOR_2/4).
    Полный BGR 1920x1080 на Pi 4 не собирается вообще.
  * Ручная экспозиция ~300 (единицы UVC, ~30 мс) держит 1080p на ~26 fps.
    Автоэкспозиция камеры в тёмной сцене сама уходит в ~15 fps, поэтому
    яркость регулируется программно через gain/brightness.

Протокол (совместим с cv_kostyl.py / unikostyl.py):
  Pi -> ноутбук:  ZMQ PUB tcp://*:5555 — на выбор два формата:
                  1) одиночный JPEG (старый протокол);
                  2) multipart [JPEG, JSON-статус] (Idea 2, включён по
                     умолчанию, --no-status возвращает старый формат).
                  JSON: {"gates": {"1": {"enabled": true, "found": true,
                                          "angle": 123, "area": 456,
                                          "role": "ours",
                                          "radius": 79,
                                          "dist_cm": 148.2,
                                          "dist_reliable": true},
                                    ...},
                         "fps": 25.9,
                         "self_mask": {"cx": 320, "cy": 180, "r": 79,
                                       "r2": 0}}
                  Ключи "1".."N" — строки; поле role (Idea 9: "ours"/
                  "opponent") добавляется ТОЛЬКО когда роль задана
                  (additive: старый ноутбук-парсер поле просто пропускает);
                  radius/held (Idea 5) и dist_cm/dist_reliable (Idea 6)
                  — опциональные поля: dist_cm есть только у найденных
                  ворот (после окна удержания T — отсутствует вместе с
                  dist_reliable=false: устаревшее значение честно
                  скрывается). Ключ self_mask (Idea 8) — применённый
                  круг исключения корпуса в px рабочего кадра,
                  присутствует только при включённой self-маске
                  (парсер ноутбука кладёт его в "extra").
                  Старый ноутбук, умеющий только одиночный JPEG, получит
                  JPEG и JSON как отдельные сообщения: JSON-часть у него
                  уйдёт в "[NET ERROR]" и будет пропущена — видео остаётся.
  Ноутбук -> Pi:  ZMQ PUB tcp://*:5557
                  {"gates":[{"id":1,"rgb":[r,g,b],"role":"ours"}, ...]}
                  (role — необязателен, только когда роль задана, Idea 9)
                  {"rgb":[r,g,b]}  (старый формат)
                  {"stream":true|false} (необязательно)
  Pi -> Arduino:  /dev/serial0, 115200: "G<id>,<angle>,<area>\\n", "HB\\n"

  Файлы:          saved_gates.json рядом со скриптом — цвета ворот
                  записываются атомарно при получении по 5557 и
                  восстанавливаются при старте: после рестарта Pi трекает
                  сохранённые ворота сразу, ноутбук не требуется (Idea 3).
                  camera_settings.json рядом со скриптом — контролы камеры
                  (Idea 1) и секция "self_mask" (Idea 8, доли кадра):
                  корпус робота вырезается из поиска цвета до inRange;
                  применяется после CLI-флагов (--self-cx/--self-cy/
                  --self-r/--self-r2, --no-self-mask).
                  distance_calib.json рядом со скриптом — калибровка
                  дистанции d = a/(b - r) - c (Idea 6): пишется
                  калибратором (out/dist_calibrate.py) атомарно, читается
                  при старте; нет файла/битый -> аналитический fallback
                  Z = W_ворот * (ширина_кадра / pi) / w_px.
"""

import argparse
import json
import math
import os
import subprocess
import sys
import threading
import time

import cv2
import numpy as np

try:
    import zmq
except ImportError:
    print("Нет pyzmq. Установите: sudo apt install python3-zmq  (или pip install pyzmq)")
    raise

try:
    import serial
except ImportError:
    serial = None

try:
    # Idea 3: сохранение/восстановление цветов ворот (saved_gates.json).
    # Модуль деплоится рядом с rpi_elp_cam.py (pscp ... gate_store.py);
    # без него Pi работает по-старому — цвета только от ноутбука по 5557.
    import gate_store
except ImportError:
    gate_store = None

# Idea 5: кластеризация кусков ворот + гистерезис потери. Модуль деплоится
# рядом с rpi_elp_cam.py (pscp ... gate_cluster.py) — ядро трекера.
import gate_cluster

try:
    # Idea 8: self-маска — вырезание корпуса робота (плата Pi, карбоновые
    # трубки) из поиска цвета ДО использования маски inRange. Модуль
    # деплоится рядом (pscp ... self_mask.py); без него Pi работает
    # по-старому — вся зона кадра участвует в поиске.
    import self_mask
except ImportError:
    self_mask = None

try:
    # Idea 6: дистанция до ворот по размеру (d = a/(b-r) - c, файл
    # distance_calib.json). Модуль деплоится рядом (pscp ... distance.py);
    # без него Pi работает по-старому — дистанция в статус-JSON не
    # попадает, на трекинг/serial это не влияет.
    import distance as dist_mod
except ImportError:
    dist_mod = None


# ===================== НАСТРОЙКИ ПО УМОЛЧАНИЮ =====================
DEFAULT_DEVICE = "/dev/video0"
DEFAULT_CAPTURE_W = 1920
DEFAULT_CAPTURE_H = 1080
DEFAULT_TRACK_W = 640
DEFAULT_TRACK_H = 360

# IP ноутбука зависит от сети: Pi и ноутбук обычно .51/.77 в одной подсети.
# Если цвета не приходят — передайте актуальный адрес через --laptop-ip.
DEFAULT_LAPTOP_IP = "10.192.226.77"
VIDEO_PORT = 5555
RGB_PORT = 5557
CAM_SETTINGS_PORT = 5559

DEFAULT_EXPOSURE = 300       # UVC exposure_time_absolute; ~26 fps на 1080p
DEFAULT_GAIN = 0             # max gain=100 даёт шум; поднимать по необходимости
DEFAULT_BRIGHTNESS = 0       # brightness=64 (max) делал картинку серой: S падала 168 -> 20
DEFAULT_GAMMA = 100
DEFAULT_SATURATION = 60
DEFAULT_CONTRAST = 32

DEFAULT_STREAM_FPS = 26        # = потолок захвата; 20 давало только ~19.5 fps видео
DEFAULT_JPEG_QUALITY = 55

SERIAL_BAUD = 115200
HEARTBEAT_INTERVAL = 0.5
ANGLE_CHANGE_THRESHOLD = 3
MIN_SEND_INTERVAL = 0.10

GATE_COUNT = 2
DEFAULT_TOLERANCE = (40, 100, 100)  # H, S, V
AREA_MIN_PIXELS_1080P = 100
MORPH_KERNEL = np.ones((3, 3), np.uint8)

# Idea 6: дистанция до ворот. Ширина ворот — из регламента (45-60 см,
# параметр турнира; аналитический fallback Z = W*(w/pi)/w_px). Удержание
# последнего достоверного значения при перекрытии ворот противником.
GATE_WIDTH_CM = 45.0
DEFAULT_DIST_HOLD = 2.0    # сек; 0 = не удерживать
DEFAULT_DIST_DROP = 0.6    # r < 0.6*r_эталона при неподвижном центроиде = заслон
DEFAULT_DIST_FILL = 0.25   # area/(w*h) ниже — «дырки» в объекте (0 = выкл)


# ===================== ЛОГИРОВАНИЕ =====================
_VERBOSE = False


def log(msg, level="INFO"):
    print(f"[{time.strftime('%H:%M:%S')}] [{level}] {msg}", flush=True)


def debug(msg):
    if _VERBOSE:
        log(msg, "DEBUG")


# ===================== V4L2 =====================
def _v4l2_run(args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=3)
    except FileNotFoundError:
        log("v4l2-ctl не найден; управление экспозицией недоступно", "WARN")
        return None
    except subprocess.TimeoutExpired:
        log("v4l2-ctl не ответил за 3 с", "WARN")
        return None


def v4l2_set(device, **controls):
    """Устанавливает UVC-контролы через v4l2-ctl. Ошибки не фатальны.

    Пачка одним вызовом быстрее, но одна неподдерживаемая связка контроля
    (например power_line_frequency сразу после auto_exposure) роняла весь
    вызов, и gamma/saturation/contrast молча не применялись. Поэтому при
    ошибке повторяем по одному контролю.
    """
    batch = ["v4l2-ctl", "-d", device]
    for name, value in controls.items():
        batch += ["-c", f"{name}={int(value)}"]

    res = _v4l2_run(batch)
    if res is None:
        return False
    if res.returncode == 0:
        return True

    ok_any = False
    for name, value in controls.items():
        single = _v4l2_run(
            ["v4l2-ctl", "-d", device, "-c", f"{name}={int(value)}"]
        )
        if single is None:
            return False
        if single.returncode == 0:
            ok_any = True
        else:
            err = (single.stderr or single.stdout).strip().replace("\n", " ")
            log(f"Контрол {name}={value} не применён ({err})", "WARN")
    return ok_any


def apply_camera_controls(device, exposure, gain, brightness, power_line=1,
                          gamma=DEFAULT_GAMMA, saturation=DEFAULT_SATURATION,
                          contrast=DEFAULT_CONTRAST):
    """Ручная экспозиция — главный способ не потерять FPS."""
    ok = v4l2_set(
        device,
        auto_exposure=1,
        exposure_time_absolute=exposure,
        gain=gain,
        brightness=brightness,
        gamma=gamma,
        saturation=saturation,
        contrast=contrast,
        exposure_dynamic_framerate=0,
        power_line_frequency=power_line,
        backlight_compensation=0,
    )
    if ok:
        log(
            f"Камера: ручная экспозиция={exposure}, gain={gain}, "
            f"brightness={brightness}, gamma={gamma}, "
            f"saturation={saturation}, contrast={contrast}, "
            f"power_line={power_line}Hz"
        )
    return ok


def restore_camera_controls(device):
    """Возвращает камере автоэкспозицию, чтобы ей могли пользоваться другие программы."""
    # exposure_time_absolute нельзя трогать в auto-режиме: контрол inactive
    # и v4l2-ctl вернёт Permission denied на всю пачку.
    v4l2_set(
        device,
        auto_exposure=3,
        gain=0,
        brightness=0,
        gamma=100,
        saturation=60,
        contrast=32,
        exposure_dynamic_framerate=1,
        backlight_compensation=1,
    )
    debug("Контролы камеры возвращены в авто-режим")


# ===================== camera_settings.json (Idea 1) =====================
# Реестр диапазонов дублирует UniKostyl/cam_settings.py CONTROLS — при
# изменении синхронизировать (файл деплоится на Pi независимо).
_CAM_SETTINGS_RANGES = {
    "exposure": (1, 5000),                # exposure_time_absolute
    "gain": (0, 100),
    "brightness": (-64, 64),
    "gamma": (72, 500),
    "saturation": (0, 128),
    "contrast": (0, 64),
    "power_line_frequency": (0, 2),       # 1 = 50 Гц
    "exposure_dynamic_framerate": (0, 1), # держим off
}


def _cam_settings_clamp(name, value, fallback):
    lo, hi = _CAM_SETTINGS_RANGES[name]
    try:
        v = int(value)
    except (TypeError, ValueError):
        v = int(fallback)
    return max(lo, min(hi, v))


def apply_camera_settings_controls(device, controls, fallback):
    """Применить UVC-контролы из словаря camera_settings.json.

    Файл и live-команды по 5559 используют одну и ту же валидацию, поэтому
    значения зажаты в те же диапазоны и AE всегда остаётся ручной.
    """
    if not isinstance(controls, dict) or not controls:
        return None
    fallback = fallback or {}
    kwargs = {
        "exposure_time_absolute": _cam_settings_clamp(
            "exposure", controls.get("exposure"), fallback.get("exposure", 300)),
        "gain": _cam_settings_clamp(
            "gain", controls.get("gain"), fallback.get("gain", 0)),
        "brightness": _cam_settings_clamp(
            "brightness", controls.get("brightness"), fallback.get("brightness", 0)),
        "gamma": _cam_settings_clamp(
            "gamma", controls.get("gamma"), fallback.get("gamma", 100)),
        "saturation": _cam_settings_clamp(
            "saturation", controls.get("saturation"), fallback.get("saturation", 60)),
        "contrast": _cam_settings_clamp(
            "contrast", controls.get("contrast"), fallback.get("contrast", 32)),
        "power_line_frequency": _cam_settings_clamp(
            "power_line_frequency", controls.get("power_line_frequency"),
            fallback.get("power_line_frequency", 1)),
        "exposure_dynamic_framerate": _cam_settings_clamp(
            "exposure_dynamic_framerate", controls.get("exposure_dynamic_framerate"),
            fallback.get("exposure_dynamic_framerate", 0)),
    }
    ok = v4l2_set(device, auto_exposure=1, **kwargs)
    if ok:
        log(f"camera_settings применены: {controls}")
    else:
        log("camera_settings не применены (v4l2-ctl отказал)", "WARN")
    return kwargs


def apply_camera_settings_file(device, path, fallback):
    """Idea 1: применить camera_settings.json (контракт PROMPT §4: файл
    пишется атомарно; применяется ПРИ СТАРТЕ ПИ ПОСЛЕ CLI-флагов).

    Файла нет -> ничего не делаем (работают CLI-флаги). Файл повреждён ->
    WARN и CLI-флаги (не роняем запуск). Автоэкспозиция всегда ручная:
    auto_exposure=1 ставится принудительно, поле "auto_exposure" из файла
    отбрасывается. Возвращает применённый словарь или None.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except Exception as e:
        log(f"camera_settings.json повреждён ({e}) — работаю с CLI-флагами", "WARN")
        return None
    if not isinstance(data, dict):
        return None
    return apply_camera_settings_controls(device, data.get("controls"), fallback)


def fourcc_str(value):
    return "".join(chr((int(value) >> (8 * i)) & 0xFF) for i in range(4))


# ===================== КАМЕРА =====================
class RawCamera:
    """V4L2-камера, отдающая сырые MJPG-кадры (без декодирования в OpenCV)."""

    def __init__(self, device, width, height, fps):
        self.device = device
        self.cap = cv2.VideoCapture(device, cv2.CAP_V4L2)
        if not self.cap.isOpened():
            raise RuntimeError(
                f"Не удалось открыть {device}. Проверьте, что камера не занята "
                f"другой программой (fuser {device}) и есть права на устройство."
            )

        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        # 0 = не декодировать MJPG внутри OpenCV; read() вернёт сырой JPEG.
        self.cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)

        # Первый read() заставляет UVC согласовать формат.
        ok, first = self.cap.read()
        if not ok or first is None:
            self.cap.release()
            raise RuntimeError("Камера открылась, но не отдала ни одного кадра")

        self.first_frame = first
        self.actual_w = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.actual_h = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.actual_fps = float(self.cap.get(cv2.CAP_PROP_FPS))
        self.actual_fourcc = int(self.cap.get(cv2.CAP_PROP_FOURCC))
        self.raw_mode = getattr(first, "ndim", 0) != 3

        if fourcc_str(self.actual_fourcc) != "MJPG":
            log(
                f"Камера вернула FOURCC={fourcc_str(self.actual_fourcc)}, "
                f"а не MJPG — FPS может быть низким",
                "WARN",
            )
        if (self.actual_w, self.actual_h) != (width, height):
            log(
                f"Запрошено {width}x{height}, камера дала "
                f"{self.actual_w}x{self.actual_h}",
                "WARN",
            )

    def read(self):
        return self.cap.read()

    def release(self):
        try:
            self.cap.release()
        except Exception:
            pass


class CaptureThread(threading.Thread):
    """Всегда вычитывает камеру, чтобы в буфере не копились старые кадры."""

    def __init__(self, camera):
        super().__init__(daemon=True, name="capture")
        self.camera = camera
        self._lock = threading.Lock()
        self._latest = None
        self._seq = 0
        self._frames = 0
        self._errors = 0
        self._stop = threading.Event()
        self._t0 = time.monotonic()

    def run(self):
        while not self._stop.is_set():
            ok, data = self.camera.read()
            if not ok or data is None:
                self._errors += 1
                time.sleep(0.01)
                continue
            if isinstance(data, np.ndarray):
                data = data.copy()
            with self._lock:
                self._latest = data
                self._seq += 1
                self._frames += 1

    def get_latest(self, last_seq):
        with self._lock:
            if self._seq == last_seq or self._latest is None:
                return None, last_seq
            return self._latest, self._seq

    def fps(self):
        dt = max(1e-6, time.monotonic() - self._t0)
        with self._lock:
            return self._frames / dt

    @property
    def errors(self):
        with self._lock:
            return self._errors

    def stop(self):
        self._stop.set()


def frame_from_raw(data, capture_w, track_w, track_h, decode_flag):
    """Декодирует MJPG сразу в уменьшенный кадр и приводит его к track-размеру."""
    if isinstance(data, np.ndarray) and data.ndim == 3:
        frame = data  # бэкенд уже отдал BGR
    else:
        frame = cv2.imdecode(data, decode_flag)
        if frame is None:
            return None

    if frame.shape[1] != track_w or frame.shape[0] != track_h:
        # Если уменьшаем — INTER_AREA, если (вдруг) увеличиваем — INTER_LINEAR.
        interp = cv2.INTER_AREA if frame.shape[1] >= track_w else cv2.INTER_LINEAR
        frame = cv2.resize(frame, (track_w, track_h), interpolation=interp)
    return frame


def choose_decode_flag(capture_w, capture_h, track_w, track_h):
    """Подбирает масштаб DCT-декодирования, чтобы не декодировать лишние пиксели."""
    if capture_w < 1000:
        return cv2.IMREAD_COLOR
    if track_w <= 420:
        return cv2.IMREAD_REDUCED_COLOR_4
    if track_w <= 1000:
        return cv2.IMREAD_REDUCED_COLOR_2
    return cv2.IMREAD_COLOR


def rotate_frame(frame, angle):
    if angle == 90:
        return cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)
    if angle == 180:
        return cv2.rotate(frame, cv2.ROTATE_180)
    if angle == 270:
        return cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)
    return frame


# ===================== ТРЕКИНГ ВОРОТ =====================
class GateTracker:
    def __init__(self, width, height, tolerance, area_min, area_scale,
                 gate_count=GATE_COUNT, merge_gap_px=None, min_area_px=None,
                 max_stray_px=None, open_kernel=gate_cluster.OPEN_KERNEL,
                 lost_frames=gate_cluster.LOST_FRAMES,
                 confirm_frames=gate_cluster.CONFIRM_FRAMES,
                 dist_model=None, dist_hold=DEFAULT_DIST_HOLD,
                 dist_drop=DEFAULT_DIST_DROP, dist_fill=DEFAULT_DIST_FILL,
                 gate_width_cm=GATE_WIDTH_CM, dist_enabled=True):
        self.width = width
        self.height = height
        self.cx0 = width // 2
        self.cy0 = height // 2
        self.tolerance = tolerance
        self.area_min = area_min
        self.area_scale = area_scale
        self.gate_count = max(1, int(gate_count))

        # Idea 5: параметры кластеризации. min_area_px по умолчанию = area_min
        # (AREA_MIN_PIXELS_1080P, уже масштабировано под разрешение),
        # merge_gap/max_stray — доли ширины кадра (2.5% / 10%).
        if min_area_px is None:
            min_area_px = area_min
        self.cluster_params = gate_cluster.resolve_params(
            width, height,
            merge_gap_px=merge_gap_px,
            min_area_px=min_area_px,
            max_stray_px=max_stray_px,
            open_kernel=open_kernel,
            lost_frames=lost_frames,
            confirm_frames=confirm_frames,
        )
        # Idea 5: гистерезис на каждые ворота (потеря через N пустых кадров,
        # подтверждение нового обнаружения M кадров, «прилипание» к прежней
        # позиции). Радиус непрерывности = max_stray_px (одна пространственная
        # шкала «того же объекта»).
        self.stability = [
            gate_cluster.GateStability(
                self.cluster_params["lost_frames"],
                self.cluster_params["confirm_frames"],
                self.cluster_params["max_stray_px"],
            )
            for _ in range(self.gate_count)
        ]
        # Накопитель времени кластеризации (для стадий в FPS-логе).
        self.cluster_time = 0.0

        # Все ворота изначально выключены: Pi сам ничего не ищет,
        # пока ноутбук не пришлёт цвета (SAVE G1..G3 -> {"gates":[...]}).
        self.params = [
            {
                "enabled": False,
                "h_ranges": [],
                "s_low": 0,
                "s_high": 255,
                "v_low": 0,
                "v_high": 255,
            }
            for i in range(self.gate_count)
        ]
        self.lowers = [[] for _ in range(self.gate_count)]
        self.uppers = [[] for _ in range(self.gate_count)]

        self.state = [
            {
                "cx": -1,
                "cy": -1,
                "angle": -1,
                "area": 0.0,
                "found": False,
                "last_sent_angle": None,
                "last_send_time": 0.0,
                "last_sent_found": False,
            }
            for _ in range(self.gate_count)
        ]

        # Переиспользуемые буферы — никаких аллокаций на каждом кадре.
        self.hsv = np.empty((height, width, 3), dtype=np.uint8)
        self.masks = [np.empty((height, width), dtype=np.uint8) for _ in range(self.gate_count)]
        self.mask_bool = np.empty((height, width), dtype=bool)
        self.tmp = np.empty((height, width), dtype=np.uint8)
        # Idea 5: метки компонент для connectedComponentsWithStats.
        self.cluster_labels = np.empty((height, width), dtype=np.int32)
        # Ядро морфологического OPEN (размер настраивается, см. --open-kernel).
        k = self.cluster_params["open_kernel"]
        self._open_kernel = (
            np.ones((k, k), np.uint8) if k > 1 else None
        )

        # Idea 8: self-маска (вырезание корпуса робота). По умолчанию ВЫКЛ
        # (None): включается вызовом set_self_mask() из run() после
        # разрешения CLI-флагов / секции self_mask в camera_settings.json.
        # Пока None, поведение трекера бит-в-бит прежнее (регресс-тесты).
        self.self_mask_params = None
        self.self_keep = None

        # Idea 6: дистанция до ворот по размеру (модель d = a/(b-r) - c из
        # distance_calib.json, без неё — аналитический fallback). Оценщик на
        # каждые ворота: защита от мусора (деление/диапазон/NaN), эвристики
        # перекрытия (внезапное уменьшение/«дырки» в объекте) и удержание
        # последнего достоверного значения T секунд. dist_mod недоступен
        # (модуль не задеплоен) или --no-dist -> None: дистанция не
        # считается, трекинг не затронут.
        self.dist_enabled = bool(dist_enabled) and dist_mod is not None
        self.dist_params = {
            "hold_s": dist_hold,
            "drop_frac": dist_drop,
            "fill_min": dist_fill,
            "gate_width_cm": gate_width_cm,
        }
        self.dist_model = None
        self.dist_est = None
        if self.dist_enabled:
            self.dist_est = [
                dist_mod.GateDistEstimator(
                    model=dist_model, hold_s=dist_hold, drop_frac=dist_drop,
                    fill_min=dist_fill, gate_width_cm=gate_width_cm,
                )
                for _ in range(self.gate_count)
            ]

    def set_distance_model(self, model):
        """Idea 6: установить/сменить калибровку (None = аналитический fallback).

        Состояние оценщиков (удержание, эталонные радиусы) сбрасывается —
        модель меняется только при старте/перезагрузке калибровки."""
        if not self.dist_enabled:
            return
        self.dist_model = model if (model is not None and model.valid) else None
        for est in self.dist_est:
            est.set_model(self.dist_model)

    def set_self_mask(self, params):
        """Idea 8: установить зону исключения корпуса (dict из
        self_mask.resolve_params / params_from_fractions) или None.

        Keep-маска (255 = искать, 0 = корпус) строится ОДИН раз на смену
        параметров/размера — в горячем цикле остаётся только один
        cv2.bitwise_and на ворота (см. process)."""
        if not params or not params.get("enabled"):
            self.self_mask_params = None
            self.self_keep = None
            return
        self.self_mask_params = dict(params)
        if self.self_keep is None or \
                self.self_keep.shape != (self.height, self.width):
            self.self_keep = np.empty((self.height, self.width), dtype=np.uint8)
        self_mask.build_keep_mask(
            self.self_mask_params, self.width, self.height, out=self.self_keep
        )

    def update_gate_from_rgb(self, gate_idx, rgb):
        if not (0 <= gate_idx < self.gate_count):
            return
        try:
            r, g, b = [int(v) for v in rgb]
        except (TypeError, ValueError):
            return

        # Ноутбук присылает именно RGB, поэтому COLOR_RGB2HSV.
        hsv_pixel = cv2.cvtColor(np.uint8([[[r, g, b]]]), cv2.COLOR_RGB2HSV)[0][0]
        h, s, v = map(int, hsv_pixel)
        tol_h, tol_s, tol_v = self.tolerance

        lo_h, hi_h = h - tol_h, h + tol_h
        if lo_h < 0:
            h_ranges = [(0, hi_h), (lo_h + 180, 179)]
        elif hi_h > 179:
            h_ranges = [(lo_h, 179), (0, hi_h - 180)]
        else:
            h_ranges = [(lo_h, hi_h)]
        h_ranges = [(max(0, int(a)), min(179, int(b))) for a, b in h_ranges if b >= a]

        s_low = max(0, s - tol_s)
        s_high = min(255, s + tol_s)
        v_low = max(0, v - tol_v)
        v_high = min(255, v + tol_v)

        lowers = [
            np.array([a, s_low, v_low], dtype=np.uint8) for a, _ in h_ranges
        ]
        uppers = [
            np.array([b, s_high, v_high], dtype=np.uint8) for _, b in h_ranges
        ]

        # Заменяем целиком — читающий поток увидит либо старое, либо новое.
        new_params = {
            "enabled": True,
            "h_ranges": h_ranges,
            "s_low": s_low,
            "s_high": s_high,
            "v_low": v_low,
            "v_high": v_high,
        }
        old_params = self.params[gate_idx]
        self.lowers[gate_idx] = lowers
        self.uppers[gate_idx] = uppers
        self.params[gate_idx] = new_params
        # Idea 5: цвет сменился -> прежняя позиция ворот не имеет смысла,
        # гистерезис сбрасывается (повторный приём ТОГО ЖЕ цвета 5 раз/с
        # состояние не трогает — иначе гистерезис никогда бы не сработал).
        if old_params != new_params:
            self.stability[gate_idx].reset()
            # Idea 6: новый цвет — прежняя дистанция/удержание смысла не имеют.
            if self.dist_est is not None:
                self.dist_est[gate_idx].reset()
        log_msg = (
            f"GATE {gate_idx + 1}: RGB={r},{g},{b} -> HSV H={h} S={s} V={v}, "
            f"H-диапазоны={h_ranges}"
        )
        # DESIGN: цвета приходят 5 раз/с с ноутбука — на INFO это 15 строк/с
        # и заметный кусок кадра в лог. Показываем только при --verbose.
        debug(log_msg)

    def process(self, gate_idx):
        """Ищет ворота на уже подготовленном self.hsv. Возвращает dict или None.

        Idea 5: конвейер inRange -> self-маска корпуса (Idea 8, обнуление
        зоны робота) -> OPEN (настраиваемое ядро) -> CLOSE ->
        connectedComponentsWithStats -> слияние близких кусков / отсев мелочи
        и «майки» (gate_cluster) -> гистерезис. Публикуемый результат —
        пост-гистерезисный: в окне грации возвращается последняя принятая
        позиция (held=True), потеря наступает только после N пустых кадров.
        """
        p = self.params[gate_idx]
        if not p["enabled"]:
            return None
        lowers = self.lowers[gate_idx]
        uppers = self.uppers[gate_idx]
        if not lowers:
            return None

        mask = self.masks[gate_idx]
        tmp = self.tmp
        cv2.inRange(self.hsv, lowers[0], uppers[0], dst=mask)
        for i in range(1, len(lowers)):
            cv2.inRange(self.hsv, lowers[i], uppers[i], dst=tmp)
            cv2.bitwise_or(mask, tmp, dst=mask)

        # Idea 8: корпус робота исключён из поиска — обнуляем зону на маске
        # КАЖДОГО цвета ДО морфологии/кластеризации (ни кусочек на корпусе,
        # ни плата Pi не могут образовать объект «ворота»). Обнулять HSV
        # нельзя: у диапазона с v_low=0/s_low=0/H-обёрткой через 0 «чёрный»
        # пиксель (H=0,S=0,V=0) всё равно проходит — карбоновые трубки
        # протекли бы именно в худшем случае. Стоимость — один
        # cv2.bitwise_and на ворота (~0.05 мс на 640x360, selfmask_check).
        if self.self_keep is not None:
            cv2.bitwise_and(mask, self.self_keep, dst=mask)

        # Морфология: OPEN (шум, ядро настраивается) + CLOSE (дырки, 3x3).
        if self._open_kernel is not None:
            cv2.morphologyEx(mask, cv2.MORPH_OPEN, self._open_kernel, dst=tmp)
            cv2.morphologyEx(tmp, cv2.MORPH_CLOSE, MORPH_KERNEL, dst=mask)
        else:
            cv2.morphologyEx(mask, cv2.MORPH_CLOSE, MORPH_KERNEL, dst=tmp)
            np.copyto(mask, tmp)

        # Idea 5: кластеризация кусков + гистерезис. connectedComponentsWithStats
        # по цене заменил cv2.moments по полной маске (он давал только сумму,
        # без отдельности кусков); findNonZero/boundingRect не возвращаем.
        prev = self.stability[gate_idx].held_xy()
        t_cl = time.perf_counter()
        det = gate_cluster.detect_gate(
            mask, self.cluster_params, prev[0] if prev else None,
            prev[1] if prev else None, labels=self.cluster_labels,
        )
        published = self.stability[gate_idx].update(det)
        self.cluster_time += time.perf_counter() - t_cl

        if published is None:
            return None

        cx = int(published["cx"])
        cy = int(published["cy"])
        # Угол — от ПЛАВОВОГО центроида кластера (паритет с pi_core
        # angle_from_center); в info идут усечённые cx/cy, как раньше.
        angle = gate_cluster.angle_deg(published["cx"], published["cy"],
                                       self.cx0, self.cy0)
        info = {
            "cx": cx,
            "cy": cy,
            "angle": angle,
            "area": published["area"] * self.area_scale,
            "area_px": published["area"],
            # Idea 5: радиус слитого объекта (описанная окружность бокса
            # кластера) и ширина кластера — для дистанции (Idea 6).
            "radius": published["radius"],
            "w_px": int(published["w"]),
        }
        if published.get("held"):
            info["held"] = True
        # Idea 6: дистанция по размеру слитого объекта (radius — та же мера,
        # что у калибратора). Оценщик сам применяет защиту (деление/диапазон
        # 0..500/NaN/экстраполяция), эвристики перекрытия (внезапное
        # уменьшение при неподвижном центроиде, «дырки» area/(w*h)) и
        # удержание последнего достоверного значения T секунд. Стоимость —
        # одна формула + проверки, микросекунды на ворота. d_cm None =
        # показывать нечего (мусор/удержание истекло) — поля в статус не
        # попадают (схема Idea 2: только у найденных ворот).
        if self.dist_est is not None:
            d_cm, d_ok = self.dist_est[gate_idx].update(
                published["radius"], published["w"], published["h"],
                published["area"], published["cx"], published["cy"],
                bool(published.get("held")), time.monotonic(),
                frame_min_dim=min(self.width, self.height),
                frame_w=self.width,
            )
            if d_cm is not None:
                info["dist_cm"] = float(d_cm)
                info["dist_reliable"] = bool(d_ok)
        return info

    def draw(self, frame, gate_idx, info, color):
        """Попиксельное маскирование: красим только пиксели, попавшие в диапазон.

        Ни рамки, ни подписи на кадре: угол уходит в лог и в Serial, а картинка
        остаётся чистым результатом маски — видно ровно те пиксели, что прошли порог.
        """
        np.greater(self.masks[gate_idx], 0, out=self.mask_bool)
        frame[self.mask_bool] = color

    def handle_serial(self, gate_idx, info, serial_link, now):
        state = self.state[gate_idx]
        gate_id = gate_idx + 1

        if info is not None:
            state["found"] = True
            state["cx"] = info["cx"]
            state["cy"] = info["cy"]
            state["angle"] = info["angle"]
            state["area"] = info["area"]
            angle = info["angle"]
            # Идея 4/контракт: разница углов с учётом перехода 0/360
            # (359 -> 1 = 2°, а не 358) — паритет с pi_core angle_diff_wrapped.
            if state["last_sent_angle"] is None:
                angle_diff = ANGLE_CHANGE_THRESHOLD
            else:
                angle_diff = abs(angle - state["last_sent_angle"]) % 360
                if angle_diff > 180:
                    angle_diff = 360 - angle_diff
            can_send = state["last_sent_angle"] is None or (
                angle_diff >= ANGLE_CHANGE_THRESHOLD
                and now - state["last_send_time"] >= MIN_SEND_INTERVAL
            )
            if can_send and serial_link is not None:
                payload = f"G{gate_id},{angle},{int(info['area'])}\n".encode()
                if serial_link.write(payload):
                    state["last_sent_angle"] = angle
                    state["last_send_time"] = now
                    state["last_sent_found"] = True
        else:
            state["found"] = False
            state["angle"] = -1
            state["area"] = 0.0
            if state["last_sent_found"] and serial_link is not None:
                if serial_link.write(f"G{gate_id},-1,0\n".encode()):
                    state["last_sent_found"] = False
                    state["last_sent_angle"] = None


# ===================== СТАТУС ДЛЯ MULTIPART-ВИДЕО (Idea 2) =====================
def build_gate_status(tracker, gate_infos, proc_fps):
    """Собирает JSON-статус ворот из состояния трекера за текущий кадр.

    gate_infos: {gate_idx: info | None} — результат tracker.process() за кадр
    (только для включённых ворот; выключенные просто отсутствуют в словаре).
    Чистая функция без аллокаций numpy — вызывается каждый кадр, стоимость
    микросекунды. Возвращает dict с числами/строками (сериализует стример).
    Схема в шапке файла; поле dist_cm/dist_reliable добавит Идея 6.
    """
    gates = {}
    for gate_idx in range(tracker.gate_count):
        enabled = bool(tracker.params[gate_idx]["enabled"])
        info = gate_infos.get(gate_idx)
        gate = {"enabled": enabled, "found": bool(info is not None)}
        # Idea 9: роль уходит в статус ТОЛЬКО когда задана ("ours"/
        # "opponent"); отсутствие поля = unset (additive: старый парсер
        # ноутбука неизвестные поля пропускает — проверено status_check).
        role = tracker.params[gate_idx].get("role")
        if role in ("ours", "opponent"):
            gate["role"] = role
        if info is not None:
            gate["angle"] = int(info["angle"])
            gate["area"] = int(round(info["area"]))
            # Idea 5 (опциональные поля, схема Idea 2 не меняется):
            # radius — радиус слитого объекта в px (мера дистанции Idea 6);
            # held=True — значение удержано гистерезисом (маска в этом
            # кадре пуста, но потеря ещё не объявлена).
            gate["radius"] = int(round(info["radius"]))
            if info.get("held"):
                gate["held"] = True
            # Idea 6 (additive, как role/radius): dist_cm — дистанция в см
            # (калиброванная модель либо аналитический fallback);
            # dist_reliable=false — значение удержано после перекрытия/
            # сомнительное (зажатое в 0..500, extrapolation и т.п.) —
            # панель/радар его скрывают. После истечения окна удержания T
            # dist_cm отсутствует вовсе (устаревшее значение честно
            # скрывается). У выключенных/потерянных ворот полей нет —
            # как angle/area.
            if info.get("dist_cm") is not None:
                gate["dist_cm"] = round(float(info["dist_cm"]), 1)
                gate["dist_reliable"] = bool(info.get("dist_reliable", False))
        gates[str(gate_idx + 1)] = gate
    try:
        fps_val = round(float(proc_fps), 1)
    except (TypeError, ValueError):
        fps_val = None
    out = {"gates": gates, "fps": fps_val}
    # Idea 8 (additive): применённый круг self-маски в px рабочего кадра.
    # Верхнеуровневый ключ: парсер ноутбука (parse_status_payload) кладёт
    # неизвестные ключи в "extra" — схема "gates"/"fps" не меняется, радар
    # (Idea 7) сможет нарисовать зону корпуса. Отсутствует при выключенной
    # маске (None) — старые ноутбуки разницы не заметят.
    sm = getattr(tracker, "self_mask_params", None)
    sm_status = self_mask.status_dict(sm) if (self_mask is not None and sm) else None
    if sm_status is not None:
        out["self_mask"] = sm_status
    return out


# ===================== SERIAL =====================
class SerialLink:
    def __init__(self, port):
        self.port = port
        self.ser = None
        self._last_heartbeat = 0.0
        if serial is None:
            log("pyserial не установлен — Arduino отключён", "WARN")
            return
        try:
            self.ser = serial.Serial(
                port, SERIAL_BAUD, timeout=0.1, write_timeout=0.2
            )
            time.sleep(1.5)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            log(f"Serial открыт: {port} @ {SERIAL_BAUD}")
        except Exception as exc:
            log(f"Serial {port} недоступен: {exc}", "WARN")
            self.ser = None

    def write(self, data):
        if self.ser is None:
            return False
        try:
            self.ser.write(data)
            self.ser.flush()
            return True
        except Exception:
            try:
                self.ser.reset_output_buffer()
            except Exception:
                pass
            return False

    def heartbeat(self, now):
        if self.ser is not None and now - self._last_heartbeat >= HEARTBEAT_INTERVAL:
            if self.write(b"HB\n"):
                self._last_heartbeat = now

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass


# ===================== ZMQ =====================
class Streamer(threading.Thread):
    """Кодирует и отправляет JPEG в приложение, не блокируя основной цикл.

    Idea 2: вместе с кадром может уходить JSON-статус ворот — тогда
    сообщение multipart [JPEG, JSON]. Статус готовится в основном цикле
    (дёшево, только скаляры), а json.dumps делается здесь, в потоке
    стримера, чтобы не тормозить горячий цикл.
    """

    def __init__(self, context, bind_port, fps, quality, enabled):
        super().__init__(daemon=True, name="streamer")
        self.context = context
        self.bind_port = bind_port
        self.fps_limit = max(1, fps)
        self.quality = int(quality)
        self.enabled = enabled
        self._lock = threading.Lock()
        self._frame = None
        self._status = None
        self._seq = 0
        self._sent = 0
        self._stop = threading.Event()
        self._t0 = time.monotonic()
        self._socket = None

    def publish(self, frame, status=None):
        with self._lock:
            self._frame = frame
            self._status = status
            self._seq += 1

    def set_enabled(self, value):
        self.enabled = bool(value)
        log(f"Видеопоток: {'ВКЛ' if self.enabled else 'ВЫКЛ'}")

    def sent_fps(self):
        dt = max(1e-6, time.monotonic() - self._t0)
        return self._sent / dt

    def run(self):
        try:
            self._socket = self.context.socket(zmq.PUB)
            self._socket.set_hwm(1)
            self._socket.setsockopt(zmq.LINGER, 0)
            self._socket.bind(f"tcp://*:{self.bind_port}")
            log(f"Видео: PUB tcp://*:{self.bind_port}")
        except Exception as exc:
            log(f"Не удалось открыть видео-порт: {exc}", "ERROR")
            return

        last_seq = -1
        last_send = 0.0
        while not self._stop.is_set():
            with self._lock:
                frame = self._frame
                status = self._status
                seq = self._seq

            if not self.enabled or frame is None or seq == last_seq:
                time.sleep(0.002)
                continue

            now = time.monotonic()
            if now - last_send < 1.0 / self.fps_limit:
                time.sleep(0.001)
                continue

            try:
                ok, jpeg = cv2.imencode(
                    ".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), self.quality]
                )
                if ok:
                    # Idea 2: multipart [JPEG, JSON-статус] только если статус
                    # есть; иначе — старый одиночный JPEG (обратная совместимость).
                    payload = None
                    if status is not None:
                        try:
                            payload = json.dumps(
                                status, separators=(",", ":")
                            ).encode()
                        except (TypeError, ValueError) as exc:
                            debug(f"Статус не сериализуется: {exc}")
                    try:
                        if payload is not None:
                            self._socket.send_multipart(
                                [jpeg.tobytes(), payload],
                                flags=zmq.NOBLOCK,
                                copy=False,
                            )
                        else:
                            self._socket.send(
                                jpeg.tobytes(), flags=zmq.NOBLOCK, copy=False
                            )
                    except zmq.Again:
                        pass
                    else:
                        self._sent += 1
            except Exception as exc:
                debug(f"Ошибка отправки видео: {exc}")

            last_seq = seq
            last_send = now

        try:
            self._socket.close()
        except Exception:
            pass

    def stop(self):
        self._stop.set()


class SettingsListener(threading.Thread):
    """Принимает CAM SET по 5559 и применяет их через v4l2-ctl на лету."""

    def __init__(self, context, laptop_ip, port, device, fallback):
        super().__init__(daemon=True, name="cam-settings-listener")
        self.context = context
        self.laptop_ip = laptop_ip
        self.port = port
        self.device = device
        self.fallback = dict(fallback or {})
        self._stop = threading.Event()
        self._socket = None

    def run(self):
        try:
            self._socket = self.context.socket(zmq.SUB)
            self._socket.setsockopt_string(zmq.SUBSCRIBE, "")
            self._socket.setsockopt(zmq.CONFLATE, 1)
            self._socket.setsockopt(zmq.RCVTIMEO, 200)
            self._socket.setsockopt(zmq.LINGER, 0)
            self._socket.connect(f"tcp://{self.laptop_ip}:{self.port}")
            log(f"CAM SET: SUB tcp://{self.laptop_ip}:{self.port}")
        except Exception as exc:
            log(f"Не удалось открыть SUB-сокет CAM SET: {exc}", "ERROR")
            return

        while not self._stop.is_set():
            try:
                msg = self._socket.recv_string()
            except zmq.Again:
                continue
            except Exception as exc:
                debug(f"Ошибка приёма CAM SET: {exc}")
                time.sleep(0.2)
                continue
            try:
                data = json.loads(msg)
            except (TypeError, ValueError):
                continue
            if not isinstance(data, dict) or data.get("cmd") != "set":
                continue
            try:
                apply_camera_settings_controls(self.device, data.get("controls"),
                                               self.fallback)
            except Exception as exc:
                log(f"Ошибка применения CAM SET: {exc}", "WARN")

        try:
            self._socket.close()
        except Exception:
            pass

    def stop(self):
        self._stop.set()


class RGBListener(threading.Thread):
    """Принимает цвета ворот и команду стрима с ноутбука."""

    def __init__(self, context, laptop_ip, port, tracker, streamer,
                 gate_store_obj=None):
        super().__init__(daemon=True, name="rgb-listener")
        self.context = context
        self.laptop_ip = laptop_ip
        self.port = port
        self.tracker = tracker
        self.streamer = streamer
        # Idea 3: копия saved_gates.json обновляется, когда цвета приходят
        # по 5557 (None — gate_store.py не найден, персистенция выключена).
        self.gate_store = gate_store_obj
        self._stop = threading.Event()
        self._socket = None
        self._got_colors = False
        self._gate_drop_warned = False

    def _effective_role(self, gate_id, role):
        """Idea 9: роль слота — с провода или прежняя (из копии файла).

        update_gate_from_rgb заменяет dict параметров ЦЕЛИКОМ, поэтому
        цвет-апдейт без роли (старый ноутбук, SAVE без смены роли) сбросил
        бы роль в статус-JSON, хотя в saved_gates.json она сохранена.
        Роль с провода ("ours"/"opponent") приоритетна; "unset" по проводy
        не передаётся (снять роль живому Pi нельзя — только рестартом,
        роль в файле тоже снимется). None = роли нет.
        """
        if role in ("ours", "opponent"):
            return role
        if self.gate_store is not None:
            prev = self.gate_store.gates.get(str(gate_id))
            if isinstance(prev, dict):
                stored = prev.get("role")
                if stored in ("ours", "opponent"):
                    return stored
        return None

    def run(self):
        try:
            self._socket = self.context.socket(zmq.SUB)
            self._socket.setsockopt_string(zmq.SUBSCRIBE, "")
            self._socket.setsockopt(zmq.CONFLATE, 1)
            self._socket.setsockopt(zmq.RCVTIMEO, 200)
            self._socket.setsockopt(zmq.LINGER, 0)
            self._socket.connect(f"tcp://{self.laptop_ip}:{self.port}")
            log(f"Цвета: SUB tcp://{self.laptop_ip}:{self.port}")
        except Exception as exc:
            log(f"Не удалось открыть SUB-сокет: {exc}", "ERROR")
            return

        t_start = time.monotonic()

        while not self._stop.is_set():
            try:
                msg = self._socket.recv_string()
            except zmq.Again:
                # Тишина +8 c почти всегда означает устаревший --laptop-ip
                # (DHCP выдал ноутбуку другой адрес). Молчать об этом нельзя:
                # иначе выглядит, будто детекция не находит цвета.
                if not self._got_colors and time.monotonic() - t_start > 8.0:
                    log(
                        f"Цвета не приходят с {self.laptop_ip}:{self.port} за 8 c — "
                        f"проверьте --laptop-ip (ipconfig на ноутбуке)",
                        "WARN",
                    )
                    t_start = float("inf")
                continue
            except Exception as exc:
                debug(f"Ошибка приёма: {exc}")
                time.sleep(0.2)
                continue

            try:
                data = json.loads(msg)
            except (TypeError, ValueError):
                continue

            if not isinstance(data, dict):
                continue

            if "stream" in data:
                self.streamer.set_enabled(bool(data["stream"]))

            self._got_colors = True

            try:
                if "gates" in data:
                    for item in data["gates"]:
                        gate_id = int(item.get("id", 1))
                        rgb = item.get("rgb")
                        if rgb is None:
                            continue
                        # Idea 9: необязательная роль ворот ("ours"/
                        # "opponent"). Поле additive: старый ноутбук его не
                        # шлёт — отсутствие/мусор роль не меняют и не
                        # сбрасывают ("unset" по проводy не передаётся).
                        role = item.get("role")
                        if role not in ("ours", "opponent"):
                            role = None
                        if gate_id - 1 >= self.tracker.gate_count:
                            # Оранжевый/синий ворота могли отправить id=3,
                            # а Pi запущен без --gates 3 — раньше это молча
                            # терялось, и угол мяча не появлялся.
                            if not self._gate_drop_warned:
                                self._gate_drop_warned = True
                                log(
                                    f"Получены цвета ворот id={gate_id}, но Pi запущен "
                                    f"с --gates {self.tracker.gate_count} — ворота "
                                    f"игнорируются. Перезапустите Pi с "
                                    f"--gates {gate_id}",
                                    "WARN",
                                )
                            continue
                        self.tracker.update_gate_from_rgb(gate_id - 1, rgb)
                        # Idea 9: роль — в параметры трекера (их читает
                        # build_gate_status для статус-JSON 5555). update_
                        # gate_from_rgb заменяет dict параметров целиком,
                        # поэтому роль ставится ПОСЛЕ него (с тела сообщения
                        # или из копии файла — см. _effective_role). Словарь
                        # параметров заменяется атомарно, ключ "role"
                        # горячий путь трекера не читает.
                        # UNVERIFIED ON HW: провод "role" 5557 -> статус 5555
                        # проверен только PC-тестами (out/role_check.py).
                        role = self._effective_role(gate_id, role)
                        if role is not None:
                            self.tracker.params[gate_id - 1]["role"] = role
                        # Idea 3: обновить копию saved_gates.json. Повторные
                        # приемы того же цвета файл не перезаписывают
                        # (GateStore.set_rgb сравнивает rgb); записывается
                        # только RGB, сохранённые LAB-пороги не трогаются.
                        # Idea 9: роль сохраняется вместе с цветом (та же
                        # пара цвет+роль — дубль, файл не переписывается).
                        if self.gate_store is not None:
                            self.gate_store.set_rgb(gate_id, rgb, role=role)
                elif "rgb" in data:
                    self.tracker.update_gate_from_rgb(0, data["rgb"])
                    # Idea 9: legacy-формат роли не несёт — держим прежнюю.
                    role = self._effective_role(1, None)
                    if role is not None:
                        self.tracker.params[0]["role"] = role
                    if self.gate_store is not None:
                        self.gate_store.set_rgb(1, data["rgb"], role=role)
            except Exception as exc:
                log(f"Ошибка разбора цвета: {exc}", "WARN")

        try:
            self._socket.close()
        except Exception:
            pass

    def stop(self):
        self._stop.set()


# ===================== ВСПОМОГАТЕЛЬНОЕ =====================
def parse_size(text):
    try:
        w, h = text.lower().replace(" ", "").split("x")
        return int(w), int(h)
    except Exception:
        raise argparse.ArgumentTypeError("Размер должен быть в виде ШИРИНАxВЫСОТА")


def parse_tolerance(text):
    try:
        values = [int(v) for v in text.replace(" ", "").split(",")]
        if len(values) != 3:
            raise ValueError
        return tuple(values)
    except Exception:
        raise argparse.ArgumentTypeError("Допуск должен быть H,S,V")


def build_parser():
    p = argparse.ArgumentParser(
        description="Оптимизированный захват ELP 180° USB-камеры на Raspberry Pi 4",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--device", default=DEFAULT_DEVICE, help="V4L2-устройство камеры")
    p.add_argument(
        "--capture-size",
        type=parse_size,
        default=(DEFAULT_CAPTURE_W, DEFAULT_CAPTURE_H),
        help="Размер захвата; 1920x1080 — единственный режим с полным FOV",
    )
    p.add_argument(
        "--track-size",
        type=parse_size,
        default=(DEFAULT_TRACK_W, DEFAULT_TRACK_H),
        help="Размер кадра для трекинга и отправки (полный кадр, без кропа)",
    )
    p.add_argument("--fps", type=int, default=30, help="Запрошенный FPS камеры")
    p.add_argument(
        "--stream-size",
        type=parse_size,
        default=None,
        help="Размер отправляемого JPEG; по умолчанию равен track-size",
    )
    p.add_argument("--stream-fps", type=int, default=DEFAULT_STREAM_FPS)
    p.add_argument("--jpeg-quality", type=int, default=DEFAULT_JPEG_QUALITY)
    p.add_argument(
        "--no-stream",
        dest="stream_on_start",
        action="store_false",
        help="Не отправлять видео до команды {\"stream\": true}",
    )
    p.set_defaults(stream_on_start=True)

    p.add_argument(
        "--no-status",
        dest="send_status",
        action="store_false",
        help="Не добавлять JSON-статус к видео 5555 "
             "(multipart [JPEG, JSON] -> старый одиночный JPEG)",
    )
    p.set_defaults(send_status=True)

    p.add_argument("--laptop-ip", default=DEFAULT_LAPTOP_IP)
    p.add_argument("--video-port", type=int, default=VIDEO_PORT)
    p.add_argument("--rgb-port", type=int, default=RGB_PORT)
    p.add_argument("--cam-settings-port", type=int, default=CAM_SETTINGS_PORT)

    p.add_argument("--serial", default="/dev/serial0", help="Порт Arduino")
    p.add_argument(
        "--no-serial", dest="serial", action="store_const", const=None,
        help="Не открывать Serial",
    )

    p.add_argument("--exposure", type=int, default=DEFAULT_EXPOSURE)
    p.add_argument("--gain", type=int, default=DEFAULT_GAIN)
    p.add_argument("--brightness", type=int, default=DEFAULT_BRIGHTNESS)
    p.add_argument("--gamma", type=int, default=DEFAULT_GAMMA,
                   help="Гамма камеры (72..500); поднимать для теней вместо brightness")
    p.add_argument("--saturation", type=int, default=DEFAULT_SATURATION,
                   help="Насыщенность камеры (0..128)")
    p.add_argument("--contrast", type=int, default=DEFAULT_CONTRAST,
                   help="Контраст камеры (0..64)")
    p.add_argument("--power-line", dest="power_line", type=int, default=1,
                   choices=(0, 1, 2),
                   help="Антифликер: 0=выкл, 1=50 Гц, 2=60 Гц")

    p.add_argument("--tolerance", type=parse_tolerance, default=DEFAULT_TOLERANCE)
    p.add_argument(
        "--gates",
        type=int,
        default=GATE_COUNT,
        help="Сколько ворот отслеживать (больше = больше нагрузки на CPU)",
    )

    # Idea 5: кластеризация кусков ворот и гистерезис потери.
    p.add_argument(
        "--merge-gap", type=float, default=None, metavar="PX",
        help="Зазор между кусками одних ворот, px (<= merge-gap сливаются в "
             "одни ворота — «тело противника»). По умолчанию 2.5%% ширины "
             "кадра (16 px при 640)",
    )
    p.add_argument(
        "--min-area", type=float, default=None, metavar="PX",
        help="Мин. площадь куска ворот, px в эквиваленте 1080p "
             "(масштабируется с разрешением; по умолчанию 100)"
    )
    p.add_argument(
        "--max-stray", type=float, default=None, metavar="PX",
        help="Куски дальше этого расстояния от основного кластера считаются "
             "чужим объектом («майка») и отбрасываются; то же значение — "
             "радиус непрерывности трека. По умолчанию 10%% ширины кадра "
             "(64 px при 640)",
    )
    p.add_argument(
        "--lost-frames", type=int, default=gate_cluster.LOST_FRAMES,
        help="Пустых кадров подряд до потери ворот: только после них уходит "
             "G<id>,-1,0 (один раз)",
    )
    p.add_argument(
        "--confirm-frames", type=int, default=gate_cluster.CONFIRM_FRAMES,
        help="Кадров подряд до принятия нового обнаружения (после потери или "
             "смещения ворот)"
    )
    p.add_argument(
        "--open-kernel", type=int, default=gate_cluster.OPEN_KERNEL,
        help="Размер ядра морфологического OPEN после inRange (нечётное, "
             "1 = отключить; подавляет мелкий шум)"
    )

    # Idea 8: self-маска — вырезание корпуса робота из поиска цвета.
    # Значения в px РАБОЧЕГО кадра (после --rotate), <=0 = авто: центр кадра,
    # r = 0.22*min(ширина, высота). Секция "self_mask" в camera_settings.json
    # (доли кадра) применяется ПОСЛЕ флагов и перекрывает их (как контролы
    # камеры Idea 1). ZONE_RADIUS_FRAC и «cut center %» — ДРУГИЕ сущности
    # (зона угла на экране / кроп RING), с self-маской не смешиваются.
    p.add_argument(
        "--self-cx", type=float, default=0.0, metavar="PX",
        help="Центр круга self-маски X, px (<=0 = центр кадра; доля ширины "
             "в секции self_mask файла настроек)"
    )
    p.add_argument(
        "--self-cy", type=float, default=0.0, metavar="PX",
        help="Центр круга self-маски Y, px (<=0 = центр кадра)"
    )
    p.add_argument(
        "--self-r", type=float, default=0.0, metavar="PX",
        help="Внутренний радиус self-маски, px (<=0 = 0.22*min(ширина,высота) "
             "~ 79 px при 640x360)"
    )
    p.add_argument(
        "--self-r2", type=float, default=0.0, metavar="PX",
        help="Внешний радиус кольца self-маски, px (0 = сплошной круг; "
             "справочно: в референсе было кольцо r 110..355 px)"
    )
    p.add_argument(
        "--no-self-mask", dest="self_mask_on", action="store_false",
        help="Выключить self-маску (корпус робота НЕ исключается из поиска)"
    )
    p.set_defaults(self_mask_on=True)

    # Idea 6: дистанция до ворот по размеру (d = a/(b-r) - c, файл
    # distance_calib.json пишет калибратор out/dist_calibrate.py).
    # Без калибровки — аналитический fallback Z = W*(w/pi)/w_px, где W —
    # ширина ворот из регламента (45-60 см, --gate-width).
    p.add_argument(
        "--dist-calib", default=None, metavar="FILE",
        help="Путь к distance_calib.json (по умолчанию рядом со скриптом); "
             "нет файла/битый — аналитическая модель"
    )
    p.add_argument(
        "--dist-hold", type=float, default=DEFAULT_DIST_HOLD, metavar="SEC",
        help="Сколько секунд держать последнее достоверное значение "
             "дистанции, когда ворота заслонены/значение сомнительное "
             "(0 = не удерживать)"
    )
    p.add_argument(
        "--dist-drop", type=float, default=DEFAULT_DIST_DROP, metavar="FRAC",
        help="Порог эвристики перекрытия: радиус упал ниже FRAC*эталона при "
             "неподвижном центроиде -> дистанция недостоверна (0 = выкл)"
    )
    p.add_argument(
        "--dist-fill", type=float, default=DEFAULT_DIST_FILL, metavar="FRAC",
        help="Мин. заполненность бокса area/(w*h): ниже — куски ворот через "
             "тело противника, дистанция недостоверна (0 = выкл)"
    )
    p.add_argument(
        "--gate-width", type=float, default=GATE_WIDTH_CM, metavar="CM",
        help="Реальная ширина ворот в см для аналитической модели "
             "(регламент 45-60; значение конкретного турнира)"
    )
    p.add_argument(
        "--no-dist", dest="dist_on", action="store_false",
        help="Не считать дистанцию (статус-JSON без dist_cm/dist_reliable)"
    )
    p.set_defaults(dist_on=True)

    p.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=0)
    p.add_argument("--threads", type=int, default=0, help="Потоки OpenCV (0 = авто)")
    p.add_argument("--duration", type=float, default=0.0, help="Работать N секунд (0 = бесконечно)")

    p.add_argument("--probe", action="store_true", help="Проверить камеру, FPS и выйти")
    p.add_argument("--snapshot", metavar="FILE", help="Сохранить кадр и выйти")
    p.add_argument("--verbose", action="store_true")
    return p


def probe_camera(args):
    """Диагностика: фактический режим, сырой FPS, скорость декодирования."""
    apply_camera_controls(
        args.device, exposure=args.exposure, gain=args.gain,
        brightness=args.brightness, gamma=args.gamma,
        saturation=args.saturation, contrast=args.contrast,
        power_line=args.power_line,
    )
    cam = RawCamera(args.device, *args.capture_size, args.fps)
    try:
        log(
            f"Фактический режим: {cam.actual_w}x{cam.actual_h} "
            f"@ {cam.actual_fps:.1f} fps, FOURCC={fourcc_str(cam.actual_fourcc)}, "
            f"raw={cam.raw_mode}"
        )

        frames = 120
        last = None
        t0 = time.perf_counter()
        n = 0
        for _ in range(frames):
            ok, data = cam.read()
            if not ok:
                break
            last = data
            n += 1
        dt = time.perf_counter() - t0
        log(f"Сырой захват: {n / dt:.1f} fps ({n} кадров за {dt:.2f} с)")

        if last is not None and getattr(last, "ndim", 0) != 3:
            for flag, name in (
                (cv2.IMREAD_COLOR, "full"),
                (cv2.IMREAD_REDUCED_COLOR_2, "reduced_2"),
                (cv2.IMREAD_REDUCED_COLOR_4, "reduced_4"),
            ):
                for _ in range(2):
                    img = cv2.imdecode(last, flag)
                k = 20
                t0 = time.perf_counter()
                for _ in range(k):
                    img = cv2.imdecode(last, flag)
                dt = time.perf_counter() - t0
                log(
                    f"Декодирование {name:9s}: {dt / k * 1000:5.2f} мс "
                    f"({k / dt:5.1f} fps), {img.shape[1]}x{img.shape[0]}"
                )
    finally:
        cam.release()
        restore_camera_controls(args.device)


def snapshot(args):
    """Сохраняет один полный (1080p) и один уменьшенный кадр — удобно проверять FOV."""
    apply_camera_controls(
        args.device, exposure=args.exposure, gain=args.gain,
        brightness=args.brightness, gamma=args.gamma,
        saturation=args.saturation, contrast=args.contrast,
        power_line=args.power_line,
    )
    cam = RawCamera(args.device, *args.capture_size, args.fps)
    try:
        for _ in range(25):  # дать камере устояться
            cam.read()
        ok, data = cam.read()
        if not ok:
            raise RuntimeError("Не удалось получить кадр")

        if isinstance(data, np.ndarray) and data.ndim == 3:
            full = data
            small = data
        else:
            full = cv2.imdecode(data, cv2.IMREAD_COLOR)
            small = cv2.imdecode(data, cv2.IMREAD_REDUCED_COLOR_2)
        if full is None or small is None:
            raise RuntimeError("Не удалось декодировать MJPG")

        tw, th = args.track_size
        small = cv2.resize(small, (tw, th), interpolation=cv2.INTER_AREA)
        base, ext = os.path.splitext(args.snapshot)
        ext = ext or ".jpg"
        full_path = f"{base}_full{ext}"
        cv2.imwrite(args.snapshot, small, [int(cv2.IMWRITE_JPEG_QUALITY), 90])
        cv2.imwrite(full_path, full, [int(cv2.IMWRITE_JPEG_QUALITY), 90])
        log(f"Сохранено: {args.snapshot} ({tw}x{th}) и {full_path} ({full.shape[1]}x{full.shape[0]})")
    finally:
        cam.release()
        restore_camera_controls(args.device)


# ===================== ОСНОВНОЙ ЦИКЛ =====================
def run(args):
    global _VERBOSE
    _VERBOSE = args.verbose

    cv2.setUseOptimized(True)
    # ВАЖНО: больше 1 потока OpenCV делает декод MJPG медленнее втрое
    # (замер на этом Pi: REDUCED_2 15.3 мс при 1 потоке против 39.5 при 4).
    # Поток камеры и поток видео и так занимают другие ядра.
    threads = args.threads or 1
    cv2.setNumThreads(max(1, threads))

    capture_w, capture_h = args.capture_size
    track_w, track_h = args.track_size
    if (capture_w, capture_h) != (1920, 1080):
        log(
            "ВНИМАНИЕ: режим камеры отличается от 1920x1080. "
            "Нижние режимы обрезают угол обзора — полный FOV не гарантирован.",
            "WARN",
        )

    log(
        f"Захват {capture_w}x{capture_h} MJPG -> обработка {track_w}x{track_h} "
        f"(полный кадр, без кропа)"
    )
    cam_settings_fallback = {
        "exposure": args.exposure, "gain": args.gain,
        "brightness": args.brightness, "gamma": args.gamma,
        "saturation": args.saturation, "contrast": args.contrast,
        "power_line_frequency": args.power_line,
        "exposure_dynamic_framerate": 0,
    }
    apply_camera_controls(
        args.device, exposure=args.exposure, gain=args.gain,
        brightness=args.brightness, gamma=args.gamma,
        saturation=args.saturation, contrast=args.contrast,
        power_line=args.power_line,
    )
    # Idea 1: сохранённые настройки камеры поверх CLI-флагов (контракт:
    # auto-применение при старте Pi ПОСЛЕ CLI; файла нет — работают CLI).
    apply_camera_settings_file(
        args.device,
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "camera_settings.json"),
        fallback=cam_settings_fallback,
    )

    camera = RawCamera(args.device, capture_w, capture_h, args.fps)
    log(
        f"Камера: {camera.actual_w}x{camera.actual_h} @ "
        f"{camera.actual_fps:.1f} fps, FOURCC={fourcc_str(camera.actual_fourcc)}"
    )

    capture = CaptureThread(camera)
    capture.start()

    serial_link = SerialLink(args.serial) if args.serial else None

    decode_flag = choose_decode_flag(
        camera.actual_w, camera.actual_h, track_w, track_h
    )
    decode_w = {cv2.IMREAD_REDUCED_COLOR_2: camera.actual_w // 2,
                cv2.IMREAD_REDUCED_COLOR_4: camera.actual_w // 4}.get(
        decode_flag, camera.actual_w
    )
    log(
        f"Декодирование: {decode_flag} (примерно {decode_w} px по ширине), "
        f"OpenCV threads={cv2.getNumThreads()}"
    )

    # После поворота рабочий кадр может стать портретным.
    if args.rotate in (90, 270):
        work_w, work_h = track_h, track_w
    else:
        work_w, work_h = track_w, track_h

    area_scale = (1920.0 * 1080.0) / float(work_w * work_h)
    area_min = max(8.0, AREA_MIN_PIXELS_1080P * work_w * work_h / (1920.0 * 1080.0))
    # Idea 5: --min-area задаётся в эквиваленте 1080p и масштабируется так же.
    min_area_px = None
    if args.min_area is not None:
        min_area_px = max(1.0, args.min_area * work_w * work_h / (1920.0 * 1080.0))

    # Idea 6: калибровка дистанции (distance_calib.json рядом со скриптом,
    # пишет калибратор out/dist_calibrate.py). Файла нет/битый/модель не
    # прошла валидацию -> None -> трекер считает по аналитической модели
    # Z = W_ворот * (ширина_кадра / pi) / w_px (форма из unikostyl.py).
    dist_model = None
    dist_calib_path = None
    if dist_mod is not None and args.dist_on:
        dist_calib_path = args.dist_calib or os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "distance_calib.json"
        )
        dist_model = dist_mod.load_model(dist_calib_path)

    tracker = GateTracker(
        work_w, work_h, args.tolerance, area_min, area_scale, args.gates,
        merge_gap_px=args.merge_gap,
        min_area_px=min_area_px,
        max_stray_px=args.max_stray,
        open_kernel=args.open_kernel,
        lost_frames=args.lost_frames,
        confirm_frames=args.confirm_frames,
        dist_model=dist_model,
        dist_hold=args.dist_hold,
        dist_drop=args.dist_drop,
        dist_fill=args.dist_fill,
        gate_width_cm=args.gate_width,
        dist_enabled=args.dist_on,
    )
    if tracker.dist_enabled:
        if dist_model is not None:
            log(
                f"Дистанция (Idea 6): калибровка "
                f"{os.path.basename(dist_calib_path)}: "
                f"a={dist_model.a:.4g}, b={dist_model.b:.4g}, "
                f"c={dist_model.c:.4g}, точек {len(dist_model.points)}, "
                f"кадр {dist_model.frame_w}x{dist_model.frame_h}"
            )
        else:
            log(
                "Дистанция (Idea 6): калибровки нет — аналитическая модель "
                f"Z = {args.gate_width:.0f} см * (ширина/π) / w_px; "
                "откалибруйте: out/dist_calibrate.py"
            )
        log(
            f"Дистанция: удержание {args.dist_hold:.1f} с при заслонении, "
            f"порог уменьшения {args.dist_drop:.2f}, "
            f"мин. заполненность {args.dist_fill:.2f}"
        )
    else:
        log(
            "Дистанция (Idea 6) выключена (--no-dist или distance.py не "
            "найден рядом со скриптом)"
        )
    cp = tracker.cluster_params
    log(
        f"Кластеризация (Idea 5): merge_gap={cp['merge_gap_px']:.0f} px, "
        f"min_area={cp['min_area_px']:.0f} px, max_stray={cp['max_stray_px']:.0f} px, "
        f"open={cp['open_kernel']}x{cp['open_kernel']}, "
        f"потеря через {cp['lost_frames']} пустых кадров, "
        f"подтверждение {cp['confirm_frames']} кадров"
    )

    # Idea 8: self-маска — корпус робота (плата Pi, карбоновые трубки в
    # центре кадра) исключается из поиска цвета ДО морфологии/кластеризации.
    # Порядок как у контролов камеры Idea 1: сначала CLI-флаги (px, <=0 =
    # авто: центр кадра + 0.22*min(w,h)), затем секция "self_mask" из
    # camera_settings.json (ДОЛИ кадра — один файл на Pi 640x360 и ноутбук
    # 720x1280) перекрывает флаги. Файла/секции нет — работают флаги.
    if self_mask is not None:
        self_params = self_mask.resolve_params(
            work_w, work_h,
            cx=(args.self_cx if args.self_cx > 0 else None),
            cy=(args.self_cy if args.self_cy > 0 else None),
            r=(args.self_r if args.self_r > 0 else None),
            r2=(args.self_r2 if args.self_r2 > 0 else None),
            enabled=args.self_mask_on,
        )
        settings_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "camera_settings.json"
        )
        section, warn = self_mask.load_section(settings_path)
        if warn:
            log(warn, "WARN")
        if section is not None:
            self_params = self_mask.params_from_fractions(
                work_w, work_h, section
            )
            log("self-маска: секция camera_settings.json перекрыла CLI-флаги")
        tracker.set_self_mask(self_params)
        sp = tracker.self_mask_params
        if sp is not None:
            if sp["r"] >= 0.45 * min(work_w, work_h):
                log(
                    f"self-маска: радиус {sp['r']:.0f} px покрывает почти весь "
                    f"кадр {work_w}x{work_h} — проверьте cx/cy/r",
                    "WARN",
                )
            annulus = (
                f", кольцо до r2={sp['r2']:.0f} px" if sp["r2"] > 0 else ""
            )
            log(
                f"Self-маска (Idea 8): центр ({sp['cx']:.0f}, {sp['cy']:.0f}), "
                f"r={sp['r']:.0f} px{annulus} — корпус исключён из поиска цвета"
            )
        else:
            log("Self-маска выключена (--no-self-mask или enabled=false)")
    else:
        log("self_mask.py не найден — корпус робота НЕ исключается из "
            "поиска (скопируйте модуль рядом с rpi_elp_cam.py)", "WARN")

    # Idea 3: восстановление ворот из saved_gates.json — Pi трекает сразу
    # после рестарта, ноутбук не требуется (главное требование Идеи 3:
    # после перезапуска цвета не выбираются заново). Хранится RGB;
    # LAB-пороги нужны только ноутбуку, на Pi они не восстанавливаются.
    gate_store_obj = None
    if gate_store is not None:
        gate_store_obj = gate_store.GateStore(min_interval=1.0)
        restored = gate_store_obj.restore_rgb()
        if restored:
            in_range = [
                (gid, rgb) for gid, rgb in restored if gid - 1 < tracker.gate_count
            ]
            skipped = [gid for gid, _ in restored if gid - 1 >= tracker.gate_count]
            for gid, rgb in in_range:
                tracker.update_gate_from_rgb(gid - 1, rgb)
                # Idea 9: роль из saved_gates.json — статус 5555 показывает
                # её сразу после рестарта Pi, без повторного SAVE. Роль
                # живёт в файле вместе с цветом слота (записывает её
                # RGBListener при приёме с 5557).
                rec = gate_store_obj.gates.get(str(gid))
                if isinstance(rec, dict):
                    rec_role = rec.get("role")
                    if rec_role in ("ours", "opponent"):
                        tracker.params[gid - 1]["role"] = rec_role
            if in_range:
                log(
                    "saved_gates.json: восстановлены ворота "
                    + ", ".join(f"G{gid} rgb={list(rgb)}" for gid, rgb in in_range)
                )
            if skipped:
                log(
                    f"saved_gates.json: ворота {skipped} не восстановлены — "
                    f"Pi запущен с --gates {tracker.gate_count}",
                    "WARN",
                )
        else:
            log("saved_gates.json: сохранённых цветов нет — жду ноутбук")
    else:
        log("gate_store.py не найден — сохранение цветов недоступно", "WARN")

    context = zmq.Context()

    stream_enabled = args.stream_on_start
    streamer = Streamer(
        context,
        args.video_port,
        args.stream_fps,
        args.jpeg_quality,
        stream_enabled,
    )
    streamer.start()

    listener = RGBListener(
        context, args.laptop_ip, args.rgb_port, tracker, streamer, gate_store_obj
    )
    listener.start()
    settings_listener = SettingsListener(
        context, args.laptop_ip, args.cam_settings_port, args.device,
        cam_settings_fallback,
    )
    settings_listener.start()

    palette = ((0, 255, 0), (255, 0, 255), (0, 255, 255), (255, 255, 0),
               (0, 128, 255), (255, 0, 0))
    overlay_colors = tuple(palette[i % len(palette)] for i in range(tracker.gate_count))
    stream_size = args.stream_size or (work_w, work_h)

    if args.stream_size and args.stream_size != (work_w, work_h):
        log(f"Видео будет уменьшено до {stream_size[0]}x{stream_size[1]}")

    last_seq = -1
    fps_count = 0
    fps_t0 = time.monotonic()
    end_time = time.monotonic() + args.duration if args.duration > 0 else None
    last_serial_hb_log = 0.0
    # Тайминги по стадиям: без них «почему 11 fps» угадывать бесполезно.
    t_dec = t_hsv = t_gates = t_stream = 0.0
    slow_hint_shown = False
    angle_log_t0 = 0.0
    # Idea 2: живой FPS обработки для JSON-статуса (EMA по межкадровым
    # интервалам — не ждём 5-секундного окна лога) и сборка статуса за кадр.
    send_status = args.send_status
    proc_fps = 0.0
    last_frame_t = None

    log(
        f"Готово. Ноутбук: {args.laptop_ip}:{args.rgb_port}, "
        f"CAM SET: :{args.cam_settings_port}, "
        f"видео: :{args.video_port}, Serial: {args.serial or 'выкл'}"
    )
    if send_status:
        log(
            "Видео 5555 идёт multipart-сообщением [JPEG, JSON-статус] (Idea 2); "
            "старый одиночный JPEG: --no-status"
        )
    if end_time is not None:
        log(f"Ограничение по времени: {args.duration:.0f} с")

    try:
        while end_time is None or time.monotonic() < end_time:
            data, seq = capture.get_latest(last_seq)
            if data is None:
                time.sleep(0.002)
                continue
            last_seq = seq

            t_a = time.perf_counter()
            frame = frame_from_raw(
                data, camera.actual_w, track_w, track_h, decode_flag
            )
            if frame is None:
                continue

            if args.rotate:
                frame = rotate_frame(frame, args.rotate)
            if frame.shape[1] != work_w or frame.shape[0] != work_h:
                interp = (
                    cv2.INTER_AREA
                    if frame.shape[1] >= work_w
                    else cv2.INTER_LINEAR
                )
                frame = cv2.resize(frame, (work_w, work_h), interpolation=interp)
            t_b = time.perf_counter()

            cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
            t_c = time.perf_counter()

            now = time.monotonic()
            any_gate = False
            frame_angles = []
            gate_infos = {}  # Idea 2: снимок результатов трекинга за кадр
            for gate_idx in range(tracker.gate_count):
                if not tracker.params[gate_idx]["enabled"]:
                    continue
                any_gate = True
                info = tracker.process(gate_idx)
                gate_infos[gate_idx] = info
                tracker.handle_serial(gate_idx, info, serial_link, now)
                if info is not None:
                    tracker.draw(
                        frame, gate_idx, info, overlay_colors[gate_idx]
                    )
                    # Idea 6: дистанция в посекундный лог; «*» = недостоверное
                    # (удержано после перекрытия / зажатый диапазон).
                    if info.get("dist_cm") is not None:
                        mark = "" if info.get("dist_reliable") else "*"
                        frame_angles.append(
                            f"G{gate_idx + 1}={info['angle']}° "
                            f"d={info['dist_cm']:.0f}см{mark}"
                        )
                    else:
                        frame_angles.append(f"G{gate_idx + 1}={info['angle']}°")
                else:
                    frame_angles.append(f"G{gate_idx + 1}=—")
            t_d = time.perf_counter()

            # Угол печатаем в лог раз в секунду — на видео его нет по задумке.
            if frame_angles and now - angle_log_t0 >= 1.0:
                log("Углы: " + ", ".join(frame_angles))
                angle_log_t0 = now

            if serial_link is not None:
                serial_link.heartbeat(now)

            # Idea 3: дописать отложенное обновление saved_gates.json.
            # Стоимость — две проверки float; сама запись происходит не чаще
            # min_interval (1 c) и только при реальном изменении цветов.
            if gate_store_obj is not None:
                gate_store_obj.flush_if_due()

            # Idea 2: FPS обработки (EMA по межкадровым интервалам) —
            # попадает в JSON-статус быстрее, чем 5-секундное окно лога.
            if last_frame_t is not None:
                dt_frame = now - last_frame_t
                if dt_frame > 0:
                    inst_fps = 1.0 / dt_frame
                    proc_fps = (
                        inst_fps if proc_fps <= 0
                        else proc_fps * 0.9 + inst_fps * 0.1
                    )
            last_frame_t = now

            if frame.shape[1] != stream_size[0] or frame.shape[0] != stream_size[1]:
                stream_frame = cv2.resize(
                    frame, stream_size, interpolation=cv2.INTER_AREA
                )
            else:
                stream_frame = frame
            # Idea 2: multipart [JPEG, JSON-статус]; при --no-status — None,
            # и стример шлёт старый одиночный JPEG.
            status = (
                build_gate_status(tracker, gate_infos, proc_fps)
                if send_status
                else None
            )
            streamer.publish(stream_frame, status)
            t_e = time.perf_counter()

            t_dec += t_b - t_a
            t_hsv += t_c - t_b
            t_gates += t_d - t_c
            t_stream += t_e - t_d

            fps_count += 1
            if now - fps_t0 >= 5.0:
                elapsed = now - fps_t0
                rate = fps_count / elapsed
                log(
                    f"FPS: обработка {fps_count / elapsed:.1f}, "
                    f"захват {capture.fps():.1f}, "
                    f"видео {streamer.sent_fps():.1f}; "
                    f"кадр {work_w}x{work_h}; "
                    f"стадии: декод {t_dec/fps_count*1000:.1f} мс, "
                    f"hsv {t_hsv/fps_count*1000:.1f}, "
                    f"ворота {t_gates/fps_count*1000:.1f} "
                    f"(кластер {tracker.cluster_time/fps_count*1000:.1f}), "
                    f"видео {t_stream/fps_count*1000:.1f}; "
                    f"экспозиция={args.exposure}, gain={args.gain}, "
                    f"brightness={args.brightness}, gamma={args.gamma}"
                )
                if capture.errors:
                    log(f"Ошибок чтения камеры: {capture.errors}", "WARN")
                # 11 fps вместо 26 — это ровно 600 МГц или потоки OpenCV.
                if rate < 20.0 and not slow_hint_shown:
                    slow_hint_shown = True
                    log(
                        "Низкий FPS. Проверьте: 1) governor = performance "
                        "(echo performance | sudo tee "
                        "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor); "
                        "2) --threads 1; 3) питание (vcgencmd get_throttled).",
                        "WARN",
                    )
                fps_count = 0
                fps_t0 = now
                t_dec = t_hsv = t_gates = t_stream = 0.0
                tracker.cluster_time = 0.0  # Idea 5: стадия кластеризации

            if not any_gate and now - last_serial_hb_log > 5.0:
                log(
                    "Ворота не активированы: Pi ждёт цвета от ноутбука — "
                    "в приложении выделите цвет рамкой R и нажмите SAVE G1..G3"
                )
                last_serial_hb_log = now

    except KeyboardInterrupt:
        log("Остановка по Ctrl+C")
    finally:
        capture.stop()
        streamer.stop()
        listener.stop()
        settings_listener.stop()
        capture.join(timeout=1.5)
        streamer.join(timeout=1.5)
        listener.join(timeout=1.5)
        settings_listener.join(timeout=1.5)
        camera.release()
        restore_camera_controls(args.device)
        # Idea 3: отложенное обновление saved_gates.json не должно
        # потеряться при остановке (мгновенная запись, лимит не смотрим).
        if gate_store_obj is not None:
            gate_store_obj.flush_pending()
        if serial_link is not None:
            serial_link.close()
        try:
            context.term()
        except Exception:
            pass
        log("Завершено")


def main():
    global _VERBOSE
    args = build_parser().parse_args()
    _VERBOSE = args.verbose

    if args.probe:
        probe_camera(args)
        return
    if args.snapshot:
        snapshot(args)
        return

    run(args)


if __name__ == "__main__":
    main()
