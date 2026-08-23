#!/usr/bin/env python3
import cv2
import zmq
import numpy as np
import time
import threading
import json
import math
import io
import serial
import sys
from picamera2 import Picamera2
from PIL import Image

# ===================== НАСТРОЙКИ =====================
LAPTOP_IP = "192.168.0.159"
VIDEO_PORT = 5555  # порт, куда отправляем видео с выделением
RGB_PORT = 5557  # порт, откуда принимаем RGB-пиксель

X_RES, Y_RES = 1280, 720
Z_SIZE = 720
CROP_SENSE_SIZE = 1080
FPS = 60
JPEG_QUALITY = 40

AREA_MIN_PIXELS = 100
AREA_MIN_M00 = AREA_MIN_PIXELS * 255.0

USE_MORPH = True
KERNEL_SIZE = 5
KERNEL = np.ones((KERNEL_SIZE, KERNEL_SIZE), np.uint8)

SERIAL_PORT = "/dev/serial0"
SERIAL_BAUD = 115200
ANGLE_CHANGE_THRESHOLD = 3  # отправлять только если изменился на >=3°
MIN_SEND_INTERVAL = 0.05  # не чаще 50 мс
HEARTBEAT_INTERVAL = 0.5  # период heartbeat, если угол не меняется

TOLERANCE = (30, 80, 80)  # расширенный допуск для HSV
LOWER_B = np.array([20, 50, 50], dtype=np.uint8)
UPPER_B = np.array([35, 255, 255], dtype=np.uint8)


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

tracking_event = threading.Event()
obj_cx = -1
obj_cy = -1
obj_angle = -1
obj_area = 0.0

center_x = Z_SIZE // 2
center_y = Z_SIZE // 2

hsv_buf = np.empty((Z_SIZE, Z_SIZE, 3), dtype=np.uint8)
mask_buf = np.empty((Z_SIZE, Z_SIZE), dtype=np.uint8)


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


# ===================== ПОТОК ПРИЁМА RGB =====================
def rgb_listener():
    global LOWER_B, UPPER_B, rgb_socket
    while True:
        try:
            if rgb_socket is None:
                if not init_zmq():
                    time.sleep(1)
                    continue
            msg = rgb_socket.recv_string(flags=zmq.NOBLOCK)
            if msg:
                data = json.loads(msg)
                if "rgb" in data:
                    r, g, b = data["rgb"]
                    hsv_pixel = cv2.cvtColor(
                        np.uint8([[[r, g, b]]]), cv2.COLOR_RGB2HSV
                    )[0][0]
                    H, S, V = map(int, hsv_pixel)
                    LOWER_B = np.array(
                        [
                            max(H - TOLERANCE[0], 0),
                            max(S - TOLERANCE[1], 0),
                            max(V - TOLERANCE[2], 0),
                        ],
                        dtype=np.uint8,
                    )
                    UPPER_B = np.array(
                        [
                            min(H + TOLERANCE[0], 179),
                            min(S + TOLERANCE[1], 255),
                            min(V + TOLERANCE[2], 255),
                        ],
                        dtype=np.uint8,
                    )
                    log(f"HSV: H={H}, S={S}, V={V} -> диапазон обновлён")
                    tracking_event.set()
        except zmq.Again:
            time.sleep(0.001)
        except Exception as e:
            log(f"Ошибка в rgb_listener: {e}", "ERROR")
            time.sleep(1)


# ===================== ОБРАБОТКА КАДРА =====================
def compute_object_info(frame_rgb):
    global obj_cx, obj_cy, obj_angle, obj_area
    cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2HSV, dst=hsv_buf)
    cv2.inRange(hsv_buf, LOWER_B, UPPER_B, dst=mask_buf)

    non_zero = cv2.countNonZero(mask_buf)
    if non_zero < 50:
        return False

    if USE_MORPH:
        tmp = np.empty_like(mask_buf)
        cv2.morphologyEx(mask_buf, cv2.MORPH_OPEN, KERNEL, dst=tmp, iterations=1)
        cv2.morphologyEx(tmp, cv2.MORPH_CLOSE, KERNEL, dst=mask_buf, iterations=1)

    M = cv2.moments(mask_buf, binaryImage=False)
    if M["m00"] <= AREA_MIN_M00:
        return False

    obj_cx = int(M["m10"] / M["m00"])
    obj_cy = int(M["m01"] / M["m00"])
    obj_area = M["m00"] / 255.0

    dx = obj_cx - center_x
    dy = center_y - obj_cy
    obj_angle = int(math.degrees(math.atan2(dy, dx)) % 360)
    return True


# ===================== КАМЕРА =====================
def init_camera():
    try:
        picam2 = Picamera2()
        config = picam2.create_video_configuration(
            main={"size": (Z_SIZE, Z_SIZE), "format": "RGB888"},
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
                "ExposureValue": -1.5,
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

    sharpen_kernel = np.array(
        [[-1, -1, -1], [-1, 9, -1], [-1, -1, -1]], dtype=np.float32
    )

    last_sent_angle = None
    last_send_time = 0.0
    no_rgb_log_time = 0

    log("Основной цикл запущен. Ожидаем RGB...")

    try:
        while True:
            frame_rgb = picam2.capture_array()
            frame_rgb = cv2.filter2D(frame_rgb, -1, sharpen_kernel)

            if tracking_event.is_set():
                found = compute_object_info(frame_rgb)
                if found:
                    # ===== РИСОВАНИЕ КОНТУРА (вместо круга и линии) =====
                    # Находим контуры на маске объекта
                    contours, _ = cv2.findContours(
                        mask_buf, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
                    )
                    cv2.drawContours(
                        frame_rgb, contours, -1, (0, 255, 0), 2
                    )  # зелёный контур толщиной 2
                    # Дополнительно выводим угол рядом с центром объекта
                    cv2.putText(
                        frame_rgb,
                        f"{obj_angle} deg",
                        (obj_cx + 10, obj_cy - 10),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.8,
                        (255, 255, 255),
                        2,
                    )
                    # =======================================================

                    now = time.time()
                    if (
                        last_sent_angle is None
                        or abs(obj_angle - last_sent_angle) >= ANGLE_CHANGE_THRESHOLD
                        and (now - last_send_time) >= MIN_SEND_INTERVAL
                    ):
                        if safe_serial_write(f"{obj_angle}\n".encode()):
                            last_sent_angle = obj_angle
                            last_send_time = now
                            log(f"Отправлен угол: {obj_angle}°")
            else:
                if time.time() - no_rgb_log_time > 5:
                    log("Ожидание RGB...")
                    no_rgb_log_time = time.time()

            # Heartbeat
            now = time.time()
            if (now - last_send_time) >= HEARTBEAT_INTERVAL:
                if safe_serial_write(b"HB\n"):
                    last_send_time = now

            # ===== ОТПРАВКА ВИДЕО С ВЫДЕЛЕНИЕМ НА НОУТБУК =====
            if video_socket is not None:
                try:
                    img = Image.fromarray(frame_rgb)
                    buf = io.BytesIO()
                    img.save(buf, format="JPEG", quality=JPEG_QUALITY)
                    video_socket.send(buf.getvalue(), flags=zmq.NOBLOCK, copy=False)
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
