import unikostyl
import cv2 as cv
import numpy as np
import zmq
import json
import io
from PIL import Image


def main_loop():
    print("[SYSTEM] Запуск сетевого узла ноутбука...")

    zmq_context = zmq.Context()
    video_sub = None
    color_pub = zmq_context.socket(zmq.PUB)
    color_pub.bind("tcp://*:5557")

    cap = cv.VideoCapture(0)  # локальная камера по умолчанию
    print(
        "[SYSTEM] Ноутбук готов. Введите IP Pi в поле 'Camera Source' и нажмите CONNECT."
    )

    while True:
        # Смена источника по запросу из интерфейса
        if unikostyl.requested_source is not None:
            src = str(unikostyl.requested_source).strip()
            unikostyl.requested_source = None

            if cap:
                cap.release()
                cap = None
            if video_sub:
                video_sub.close()
                video_sub = None

            if "." in src:  # IP Pi
                video_sub = zmq_context.socket(zmq.SUB)
                video_sub.setsockopt_string(zmq.SUBSCRIBE, "")
                video_sub.setsockopt(zmq.CONFLATE, 1)
                video_sub.connect(f"tcp://{src}:5555")
                print(f"[SYSTEM] Подключено к видео Pi: {src}")
            else:
                cap = cv.VideoCapture(int(src))
                print(f"[SYSTEM] Подключено к локальной камере: {src}")

        # Получение кадра в RGB
        frame_rgb = None
        if video_sub:
            try:
                msg = video_sub.recv(flags=zmq.NOBLOCK)
                # JPEG декодируем сразу в RGB через PIL – никаких BGR!
                image = Image.open(io.BytesIO(msg))
                frame_rgb = np.array(image.convert("RGB"))
                # При необходимости можно повернуть:
                # frame_rgb = np.rot90(frame_rgb, k=1)  # пример поворота на 90°
            except zmq.Again:
                pass
            except Exception as e:
                print(f"[NET ERROR] {e}")
        elif cap and cap.isOpened():
            ret, f = cap.read()
            if ret:
                f = cv.rotate(f, cv.ROTATE_90_COUNTERCLOCKWISE)
                # Локальная веб-камера отдаёт BGR, конвертируем в RGB
                frame_rgb = cv.cvtColor(f, cv.COLOR_BGR2RGB)

        # Запуск интерфейса (передаем чистый RGB кадр)
        _, pending_rgb = unikostyl.main_loop_frame(frame_rgb)

        # Если нажали SAVE – отправляем RGB на Pi
        if pending_rgb is not None:
            # Приводим к обычным int на случай numpy типов
            r, g, b = int(pending_rgb[0]), int(pending_rgb[1]), int(pending_rgb[2])
            payload = json.dumps({"rgb": [r, g, b]})
            color_pub.send_string(payload)
            print(f"[NETWORK] --> Отправлен RGB: {payload}")

    if cap:
        cap.release()
    if video_sub:
        video_sub.close()
    color_pub.close()
    zmq_context.term()


if __name__ == "__main__":
    main_loop()
