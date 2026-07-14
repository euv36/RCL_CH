import math
import threading
import time
from picamera2 import Picamera2
import cv2
import json
import numpy as np
import serial  # --- ДЛЯ ARDUINO ---
import zmq

LAPTOP_IP = "10.248.106.77"

# --- ИНИЦИАЛИЗАЦИЯ ARDUINO (Пины 8 и 10) ---
SERIAL_PORT = "/dev/serial0"
BAUD_RATE = 9600
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    print(f"[UART] Порт {SERIAL_PORT} успешно открыт.")
    time.sleep(2)  # Пауза на инициализацию Arduino после открытия порта
except Exception as e:
    print(f"[UART ERROR] Не удалось открыть порт {SERIAL_PORT}: {e}")
    ser = None

# Переменная для хранения последнего отправленного угла
last_sent_angle = -1

context = zmq.Context()
video_socket = context.socket(zmq.PUB)
video_socket.set_hwm(1)
video_socket.bind("tcp://*:5555")

rgb_socket = context.socket(zmq.SUB)
rgb_socket.setsockopt_string(zmq.SUBSCRIBE, "")
rgb_socket.setsockopt(zmq.CONFLATE, 1)
rgb_socket.connect(f"tcp://{LAPTOP_IP}:5557")

target_rgb = None
tracking_enabled = False
target_lab = None


def rgb_listener():
    global target_rgb, tracking_enabled, target_lab
    print(f"[ZMQ] Ожидаю RGB от {LAPTOP_IP}:5557 ...")
    while True:
        try:
            msg = rgb_socket.recv_string(flags=zmq.NOBLOCK)
            data = json.loads(msg)
            if "rgb" in data:
                target_rgb = data["rgb"]
                tracking_enabled = True
                target_lab = None
                print(f"\n[RGB] Получен цвет: {target_rgb} -> трекинг ВКЛЮЧЁН")
        except zmq.Again:
            time.sleep(0.002)
        except Exception as e:
            print(f"\n[ZMQ ERROR] {e}")
            time.sleep(1)


threading.Thread(target=rgb_listener, daemon=True).start()


def rgb_to_lab(rgb):
    r, g, b = rgb
    rgb_arr = np.uint8([[[r, g, b]]])
    lab_arr = cv2.cvtColor(rgb_arr, cv2.COLOR_RGB2LAB)
    L = int(lab_arr[0, 0, 0]) * 100 // 255
    a = int(lab_arr[0, 0, 1]) - 128
    b_ = int(lab_arr[0, 0, 2]) - 128
    return (L, a, b_)


def find_object_and_draw(frame_rgb, target_lab, tolerance=30):
    lab = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2LAB).astype(np.int16)
    L = lab[:, :, 0] * 100 // 255
    a = lab[:, :, 1] - 128
    b = lab[:, :, 2] - 128

    Lmin = max(0, target_lab[0] - tolerance)
    Lmax = min(100, target_lab[0] + tolerance)
    Amin = max(-128, target_lab[1] - tolerance)
    Amax = min(127, target_lab[1] + tolerance)
    Bmin = max(-128, target_lab[2] - tolerance)
    Bmax = min(127, target_lab[2] + tolerance)

    mask = (
        (L >= Lmin)
        & (L <= Lmax)
        & (a >= Amin)
        & (a <= Amax)
        & (b >= Bmin)
        & (b <= Bmax)
    )
    mask = mask.astype(np.uint8) * 255

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    h, w = frame_rgb.shape[:2]
    center_x, center_y = w // 2, h // 2
    angle = -1

    out_frame = frame_rgb.copy()
    if contours:
        largest = max(contours, key=cv2.contourArea)
        if cv2.contourArea(largest) > 300:
            M = cv2.moments(largest)
            if M["m00"] != 0:
                cx = int(M["m10"] / M["m00"])
                cy = int(M["m01"] / M["m00"])
                dx = cx - center_x
                dy = center_y - cy
                angle = int(math.degrees(math.atan2(dy, dx)) % 360)
                cv2.drawContours(out_frame, [largest], -1, (0, 255, 0), 2)
                cv2.line(out_frame, (center_x, center_y), (cx, cy), (255, 0, 0), 2)
    cv2.circle(out_frame, (center_x, center_y), 5, (0, 255, 0), -1)
    return angle, out_frame


# --- ИНИЦИАЛИЗАЦИЯ PICAMERA2 ---
picam2 = Picamera2()
config = picam2.create_video_configuration(
    main={"size": (640, 480), "format": "RGB888"}, controls={"FrameRate": 30}
)
picam2.configure(config)
picam2.start()

print("wait rgb")

try:
    while True:
        frame_rgb = picam2.capture_array()

        if tracking_enabled and target_rgb is not None:
            if target_lab is None:
                target_lab = rgb_to_lab(target_rgb)
                print(f"[TRACK] Целевой LAB: {target_lab}")
            angle, frame_with_contours = find_object_and_draw(
                frame_rgb, target_lab, tolerance=30
            )

            # --- ДЛЯ ARDUINO: Проверка изменения угла ---
            if angle != -1:
                if angle != last_sent_angle:
                    print(f"[ANGLE] Угол изменился: {angle}° -> Отправка")
                    if ser and ser.is_open:
                        try:
                            # Отправляем новый угол текстовой строкой с \n в конце
                            ser.write(f"{angle}\n".encode("utf-8"))
                            last_sent_angle = angle
                        except Exception as e:
                            print(f"[UART WRITE ERROR] {e}")
                else:
                    # Угол такой же, как в прошлый раз — ничего не шлем
                    pass
            frame_to_send = frame_with_contours
        else:
            frame_to_send = frame_rgb

        # Отправляем кадр на ноутбук
        frame_bgr_to_send = cv2.cvtColor(frame_to_send, cv2.COLOR_RGB2BGR)
        _, buffer = cv2.imencode(
            ".jpg", frame_bgr_to_send, [cv2.IMWRITE_JPEG_QUALITY, 50]
        )
        video_socket.send(buffer.tobytes())

except KeyboardInterrupt:
    print("\n[INFO] Завершение по Ctrl+C...")
finally:
    if "picam2" in locals():
        picam2.stop()
    if ser and ser.is_open:
        ser.close()
        print("[UART] Порт закрыт.")
    video_socket.close()
    rgb_socket.close()
    context.term()
    print("[INFO] Программа завершена.")
