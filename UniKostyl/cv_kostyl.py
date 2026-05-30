import unikostyl
import cv2 as cv
import numpy as np
import zmq
import time
import sys

def main_loop():
    print("[SYSTEM] Инициализация сети и камеры...")

    zmq_context = zmq.Context()

    # 1. Сокет для ПРИЕМА видео от Pi
    video_sub = None

    # 2. Сокет для ОТПРАВКИ угла на Pi (Publisher)
    angle_pub = zmq_context.socket(zmq.PUB)
    angle_pub.bind("tcp://*:5556") # Ноутбук открывает этот порт для Малины

    cap = cv.VideoCapture(0)
    current_source = "0"

    print("[SYSTEM] Система готова. Ожидание команд...")

    while True:
        # ПРОВЕРКА СМЕНЫ ИСТОЧНИКА
        if unikostyl.requested_source is not None:
            src = str(unikostyl.requested_source).strip()
            unikostyl.requested_source = None

            if cap: cap.release(); cap = None
            if video_sub: video_sub.close(); video_sub = None

            if "." in src: # Если ввели IP
                video_sub = zmq_context.socket(zmq.SUB)
                video_sub.setsockopt_string(zmq.SUBSCRIBE, "")
                video_sub.setsockopt(zmq.CONFLATE, 1)
                video_sub.connect(f"tcp://{src}:5555")
                print(f"[SYSTEM] Подключено к видео Pi: {src}")
            else:
                cap = cv.VideoCapture(int(src))

        # ПОЛУЧЕНИЕ КАДРА
               # ПОЛУЧЕНИЕ КАДРА
        frame_rgb = None
        if video_sub:
            try:
                message = video_sub.recv(flags=zmq.NOBLOCK)
                f = cv.imdecode(np.frombuffer(message, dtype=np.uint8), cv.IMREAD_COLOR)
                if f is not None:
                    # УДАЛИЛИ СТРОКУ С cv.rotate, так как Малина уже повернула кадр сама

                    frame_rgb = cv.cvtColor(f, cv.COLOR_BGR2RGB)
            except zmq.Again: pass
        elif cap and cap.isOpened():
            ret, f = cap.read()
            if ret:
                f = cv.rotate(f, cv.ROTATE_90_COUNTERCLOCKWISE)
                frame_rgb = cv.cvtColor(f, cv.COLOR_BGR2RGB)


        # ВЫЗОВ ИНТЕРФЕЙСА И ПОЛУЧЕНИЕ УГЛА
        # Теперь unikostyl.main_loop_frame возвращает вычисленный угол
        angle = unikostyl.main_loop_frame(frame_rgb)

        # ОТПРАВКА УГЛА ОБРАТНО НА PI
        if angle is not None and angle != -1:
            print(f"--> ОТПРАВЛЯЮ УГОЛ: {angle}") # ДОБАВЬ ЭТУ СТРОКУ
            angle_pub.send_string(str(angle))

    if cap: cap.release()
    angle_pub.close()

if __name__ == "__main__":
    main_loop()
