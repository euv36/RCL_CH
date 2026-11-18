#!/usr/bin/env python3
import cv2
import zmq
import numpy as np
import time
import threading
import json
import math
import serial
import sys
from picamera2 import Picamera2

# ===================== НАСТРОЙКИ =====================
LAPTOP_IP = "172.17.1.25"
VIDEO_PORT = 5555  # порт, куда отправляем видео с выделением
RGB_PORT = 5557  # порт, откуда принимаем RGB-пиксели ворот

X_RES, Y_RES = 1280, 720
# 640x640@30 — безопасный runtime-режим для Raspberry Pi 4.
# Если нужна максимальная детализация, можно поднять до 720, но тогда
# проверь фактический FPS в логе.
Z_SIZE = 640
CROP_SENSE_SIZE = 1080
FPS = 20
JPEG_QUALITY = 40
VIDEO_FPS = 20  # видео в приложение отправляем не чаще 20 раз в секунду
USE_SHARPEN = False  # sharpen заметно нагружает CPU и не нужен для трекинга
ENABLE_VIDEO = True  # False = вообще не кодировать/отправлять видео (максимум FPS)
DRAW_OVERLAYS = True  # False = не искать контуры и не рисовать их на видео

AREA_MIN_PIXELS = 100
AREA_MIN_M00 = AREA_MIN_PIXELS * 255.0

USE_MORPH = True
KERNEL_SIZE = 5
KERNEL = np.ones((KERNEL_SIZE, KERNEL_SIZE), np.uint8)

SERIAL_PORT = "/dev/serial0"
SERIAL_BAUD = 115200  # должен совпадать с Arduino Serial2
ANGLE_CHANGE_THRESHOLD = 3  # отправлять только если изменился на >=3°
MIN_SEND_INTERVAL = 0.05  # не чаще 50 мс для одного типа ворот
HEARTBEAT_INTERVAL = 0.5  # период heartbeat, если угол не меняется

# Два независимых типа ворот.
GATE_COUNT = 2
GATE_COLORS = [(0, 255, 0), (255, 0, 255)]  # цвета контуров на видео (BGR/RGB green, magenta)
TOLERANCE = (30, 80, 80)  # расширенный допуск HSV вокруг выбранного цвета


# ===================== ЛОГИРОВАНИЕ =====================
def log(msg, level="INFO"):
    t = time.strftime("%H:%M:%S")
    print(f"[{t}] [{level}] {msg}")
    sys.stdout.flush()


# ===================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =====================
context = None
video_socket = None
rgb_socket = None
ser = None

# Параметры HSV для каждого типа ворот. Ворота 1 по умолчанию желтые,
# ворота 2 выключены до получения цвета из приложения.
gate_params = [
    {
        "enabled": True,
        "h_ranges": [(20, 35)],
        "s_low": 50,
        "s_high": 255,
        "v_low": 50,
        "v_high": 255,
    },
    {
        "enabled": False,
        "h_ranges": [],
        "s_low": 0,
        "s_high": 255,
        "v_low": 0,
        "v_high": 255,
    },
]

# Состояние трекинга каждого типа ворот.
gate_state = [
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
    for _ in range(GATE_COUNT)
]

center_x = Z_SIZE // 2
center_y = Z_SIZE // 2

hsv_buf = np.empty((Z_SIZE, Z_SIZE, 3), dtype=np.uint8)
# Переиспользуемые буферы, чтобы не выделять память на каждом кадре.
mask_bufs = [np.empty((Z_SIZE, Z_SIZE), dtype=np.uint8) for _ in range(GATE_COUNT)]
tmp_mask_buf = np.empty((Z_SIZE, Z_SIZE), dtype=np.uint8)


# ===================== ZMQ =====================
def init_zmq():
    global context, video_socket, rgb_socket
    try:
        if context is not None:
            context.term()
            time.sleep(0.1)
        context = zmq.Context()
        video_socket = context.socket(zmq.PUB)
        video_socket.set_hwm(1)
        video_socket.setsockopt(zmq.LINGER, 0)
        video_socket.bind(f"tcp://*:{VIDEO_PORT}")

        rgb_socket = context.socket(zmq.SUB)
        rgb_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        rgb_socket.setsockopt(zmq.CONFLATE, 1)
        rgb_socket.setsockopt(zmq.RCVTIMEO, 100)
        rgb_socket.connect(f"tcp://{LAPTOP_IP}:{RGB_PORT}")
        log("ZMQ инициализирован")
        return True
    except Exception as e:
        log(f"Ошибка ZMQ: {e}", "ERROR")
        return False


# ===================== SERIAL =====================
def init_serial():
    global ser
    try:
        ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0.1, write_timeout=0.2)
        time.sleep(2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        log("Serial порт открыт")
        return True
    except Exception as e:
        log(f"Ошибка Serial: {e}", "ERROR")
        ser = None
        return False


def safe_serial_write(data):
    if ser is None:
        return False
    try:
        ser.write(data)
        ser.flush()
        return True
    except Exception:
        try:
            ser.reset_output_buffer()
        except:
            pass
        return False


# ===================== ЦВЕТА ВОРОТ =====================
def update_gate_from_rgb(gate_idx, rgb):
    """
    Пересчитывает HSV-диапазон ворот gate_idx (0..GATE_COUNT-1) по RGB,
    который пришел из приложения. Поддерживает переход Hue через 0/179.
    """
    global gate_params
    r, g, b = [int(v) for v in rgb]
    hsv_pixel = cv2.cvtColor(np.uint8([[[r, g, b]]]), cv2.COLOR_RGB2HSV)[0][0]
    H, S, V = map(int, hsv_pixel)
    tol_h, tol_s, tol_v = TOLERANCE

    lo_h = H - tol_h
    hi_h = H + tol_h
    if lo_h < 0:
        # Красный/розовый цвет: диапазон Hue переходит через 0.
        h_ranges = [(0, hi_h), (lo_h + 180, 179)]
    elif hi_h > 179:
        # Диапазон переходит через 179.
        h_ranges = [(lo_h, 179), (0, hi_h - 180)]
    else:
        h_ranges = [(lo_h, hi_h)]

    h_ranges = [(max(0, int(a)), min(179, int(b))) for a, b in h_ranges if b >= a]

    gate_params[gate_idx] = {
        "enabled": True,
        "h_ranges": h_ranges,
        "s_low": max(0, int(S - tol_s)),
        "s_high": min(255, int(S + tol_s)),
        "v_low": max(0, int(V - tol_v)),
        "v_high": min(255, int(V + tol_v)),
    }
    return H, S, V


# ===================== ПОТОК ПРИЁМА RGB =====================
def apply_gate_message(data):
    """
    Применяет одно JSON-сообщение с цветами ворот.
    Поддерживает новый и старый форматы. Возвращает список примененных
    обновлений [(gate_id, H, S, V), ...] — удобно для тестов.
    """
    updates = []

    # Новый формат: {"gates": [{"id": 1, "rgb": [r,g,b]}, ...]}
    if "gates" in data:
        for item in data["gates"]:
            try:
                gate_id = int(item.get("id", 0))
                rgb = item.get("rgb")
            except (TypeError, ValueError, AttributeError):
                continue
            if rgb is not None and 1 <= gate_id <= GATE_COUNT:
                updates.append((gate_id, rgb))
    # Старый формат: {"rgb": [r,g,b]} — обновляет ворота 1.
    elif "rgb" in data:
        updates.append((1, data["rgb"]))

    applied = []
    for gate_id, rgb in updates:
        gate_idx = gate_id - 1
        H, S, V = update_gate_from_rgb(gate_idx, rgb)
        applied.append((gate_id, H, S, V))
        log(
            f"GATE {gate_id}: RGB={list(rgb)} -> HSV H={H}, S={S}, V={V}, "
            f"H-диапазоны={gate_params[gate_idx]['h_ranges']}"
        )
    return applied


def rgb_listener():
    global rgb_socket
    while True:
        try:
            if rgb_socket is None:
                if not init_zmq():
                    time.sleep(1)
                    continue
            msg = rgb_socket.recv_string(flags=zmq.NOBLOCK)
            if msg:
                apply_gate_message(json.loads(msg))
        except zmq.Again:
            time.sleep(0.001)
        except Exception as e:
            log(f"Ошибка в rgb_listener: {e}", "ERROR")
            time.sleep(1)


# ===================== ОБРАБОТКА КАДРА =====================
def build_gate_mask(hsv_frame, gate_idx, mask, tmp):
    """
    Строит маску ворот по HSV-параметрам конкретного типа ворот.
    mask/tmp — заранее выделенные буферы, чтобы не мусорить аллокациями.
    """
    p = gate_params[gate_idx]
    mask.fill(0)

    # Если Hue-диапазон переходит через 0/179, масок может быть две.
    for i, (h_low, h_high) in enumerate(p["h_ranges"]):
        lower = np.array([h_low, p["s_low"], p["v_low"]], dtype=np.uint8)
        upper = np.array([h_high, p["s_high"], p["v_high"]], dtype=np.uint8)
        if i == 0:
            cv2.inRange(hsv_frame, lower, upper, dst=mask)
        else:
            cv2.inRange(hsv_frame, lower, upper, dst=tmp)
            cv2.bitwise_or(mask, tmp, dst=mask)

    return mask


def compute_gate_info(hsv_frame, gate_idx, mask, tmp):
    """
    Ищет объект заданного типа ворот на HSV-кадре.
    Возвращает (info, mask), где info=None, если ворота не найдены.
    """
    mask = build_gate_mask(hsv_frame, gate_idx, mask, tmp)

    if cv2.countNonZero(mask) < 50:
        return None, mask

    if USE_MORPH:
        cv2.morphologyEx(mask, cv2.MORPH_OPEN, KERNEL, dst=tmp, iterations=1)
        cv2.morphologyEx(tmp, cv2.MORPH_CLOSE, KERNEL, dst=mask, iterations=1)

    M = cv2.moments(mask, binaryImage=False)
    if M["m00"] <= AREA_MIN_M00:
        return None, mask

    cx = int(M["m10"] / M["m00"])
    cy = int(M["m01"] / M["m00"])
    area = M["m00"] / 255.0

    dx = cx - center_x
    dy = center_y - cy
    angle = int(math.degrees(math.atan2(dy, dx)) % 360)

    return {"cx": cx, "cy": cy, "angle": angle, "area": area}, mask


def send_gate_update(gate_idx, info):
    """Отправляет угол/площадь ворот, если угол изменился достаточно сильно."""
    state = gate_state[gate_idx]
    now = time.time()

    can_send = (
        state["last_sent_angle"] is None
        or (
            abs(info["angle"] - state["last_sent_angle"]) >= ANGLE_CHANGE_THRESHOLD
            and (now - state["last_send_time"]) >= MIN_SEND_INTERVAL
        )
    )
    if not can_send:
        return

    gate_id = gate_idx + 1
    payload = f"G{gate_id},{info['angle']},{int(info['area'])}\n".encode()
    if safe_serial_write(payload):
        state["last_sent_angle"] = info["angle"]
        state["last_send_time"] = now
        state["last_sent_found"] = True
        log(f"TX G{gate_id}: {payload!r}")


def send_gate_lost(gate_idx):
    """Один раз сообщает Arduino, что конкретный тип ворот потерян."""
    state = gate_state[gate_idx]
    if not state["last_sent_found"]:
        return

    gate_id = gate_idx + 1
    payload = f"G{gate_id},-1,0\n".encode()
    if safe_serial_write(payload):
        state["last_sent_found"] = False
        state["last_sent_angle"] = None
        log(f"TX G{gate_id}: ворота потеряны")


# ===================== КАМЕРА =====================
def init_camera():
    try:
        picam2 = Picamera2()
        config = picam2.create_video_configuration(
            main={"size": (Z_SIZE, Z_SIZE), "format": "BGR888"},
            controls={"FrameRate": FPS},
        )
        crop_left = (X_RES - CROP_SENSE_SIZE) // 2
        crop_top = (Y_RES - CROP_SENSE_SIZE) // 2
        config["main"]["ScalerCrop"] = (
            crop_left,
            crop_top,
            CROP_SENSE_SIZE,
            CROP_SENSE_SIZE,
        )
        picam2.configure(config)
        picam2.start()
        picam2.set_controls(
            {
                "ExposureValue": 0,
                "AeMeteringMode": 1,
                "NoiseReductionMode": 2,
                "Sharpness": 3.0,
            }
        )
        log("Камера запущена")
        return picam2
    except Exception as e:
        log(f"Ошибка камеры: {e}", "ERROR")
        return None


# ===================== ГЛАВНАЯ ФУНКЦИЯ =====================
def main():
    global context, video_socket, rgb_socket, ser

    if not init_zmq():
        return
    init_serial()

    picam2 = init_camera()
    if picam2 is None:
        return

    threading.Thread(target=rgb_listener, daemon=True).start()

    sharpen_kernel = (
        np.array([[-1, -1, -1], [-1, 9, -1], [-1, -1, -1]], dtype=np.float32)
        if USE_SHARPEN
        else None
    )

    last_heartbeat_time = 0.0
    last_video_time = 0.0
    no_rgb_log_time = 0.0
    fps_count = 0
    fps_start = time.time()

    log("Основной цикл запущен. Ожидаем цвета ворот...")

    try:
        while True:
            frame = picam2.capture_array()
            if USE_SHARPEN:
                frame = cv2.filter2D(frame, -1, sharpen_kernel)

            # Один раз на кадр переводим изображение в HSV.
            cv2.cvtColor(frame, cv2.COLOR_RGB2HSV, dst=hsv_buf)

            any_enabled = False
            for gate_idx in range(GATE_COUNT):
                if not gate_params[gate_idx]["enabled"]:
                    continue

                any_enabled = True
                info, mask = compute_gate_info(
                    hsv_buf,
                    gate_idx,
                    mask_bufs[gate_idx],
                    tmp_mask_buf,
                )
                state = gate_state[gate_idx]

                if info is not None:
                    state["cx"] = info["cx"]
                    state["cy"] = info["cy"]
                    state["angle"] = info["angle"]
                    state["area"] = info["area"]
                    state["found"] = True

                    if DRAW_OVERLAYS:
                        # Рисуем контур и подпись своего цвета.
                        contours, _ = cv2.findContours(
                            mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
                        )
                        cv2.drawContours(frame, contours, -1, GATE_COLORS[gate_idx], 2)
                        cv2.putText(
                            frame,
                            f"G{gate_idx + 1} {info['angle']}",
                            (info["cx"] + 10, info["cy"]),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.6,
                            GATE_COLORS[gate_idx],
                            2,
                        )

                    send_gate_update(gate_idx, info)
                else:
                    state["found"] = False
                    state["angle"] = -1
                    state["area"] = 0.0
                    send_gate_lost(gate_idx)

            if not any_enabled and time.time() - no_rgb_log_time > 5:
                log("Ожидание RGB...")
                no_rgb_log_time = time.time()

            now = time.time()

            # Периодически показываем фактический FPS обработки — это главный
            # показатель, хватает ли Raspberry Pi 4 ресурсов в runtime.
            fps_count += 1
            if now - fps_start >= 5.0:
                elapsed = now - fps_start
                log(f"FPS обработки: {fps_count / elapsed:.1f}")
                fps_count = 0
                fps_start = now

            if (now - last_heartbeat_time) >= HEARTBEAT_INTERVAL:
                if safe_serial_write(b"HB\n"):
                    last_heartbeat_time = now

            # Видео в приложение отправляем с ограничением VIDEO_FPS, чтобы
            # JPEG-кодирование не съедало CPU каждый кадр.
            if (
                ENABLE_VIDEO
                and video_socket is not None
                and (now - last_video_time) >= (1.0 / VIDEO_FPS)
            ):
                try:
                    # Сохраняем прежнее соглашение по каналам: кадр считается
                    # RGB, поэтому для JPEG переводим его в BGR.
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                    ok, jpeg = cv2.imencode(
                        ".jpg",
                        frame_bgr,
                        [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY],
                    )
                    if ok:
                        video_socket.send(jpeg.tobytes(), flags=zmq.NOBLOCK, copy=False)
                        last_video_time = now
                except Exception as e:
                    log(f"Ошибка отправки видео: {e}", "WARN")
                    video_socket = None

    except KeyboardInterrupt:
        log("Программа остановлена")
    except Exception as e:
        log(f"Критическая ошибка: {e}", "ERROR")
        import traceback

        traceback.print_exc()
    finally:
        picam2.stop()
        if video_socket:
            video_socket.close()
        if rgb_socket:
            rgb_socket.close()
        if context:
            context.term()
        if ser and ser.is_open:
            ser.close()
        log("Завершено")


if __name__ == "__main__":
    main()
