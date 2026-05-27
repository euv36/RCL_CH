import unikostyl
import cv2 as cv
import numpy as np
import zmq
import time

def main_loop():
    current_source = "0"
    cap = None
    zmq_socket = None
    zmq_context = zmq.Context()

    print("Система захвата готова.")

    while True:
        # ПРОВЕРКА СМЕНЫ ИСТОЧНИКА
        if unikostyl.requested_source is not None:
            new_src = str(unikostyl.requested_source)
            unikostyl.requested_source = None

            # Закрываем всё старое
            if cap: cap.release(); cap = None
            if zmq_socket: zmq_socket.close(); zmq_socket = None

            current_source = new_src
            print(f"Переключение на: {current_source}")

            # Если это URL или IP — считаем, что это ZMQ поток от Малины
            if "http" in current_source or "." in current_source:
                # Чистим адрес от http:// и портов, если ввели лишнее
                ip = current_source.replace("http://", "").split(":")[0]
                zmq_socket = zmq_context.socket(zmq.SUB)
                zmq_socket.setsockopt_string(zmq.SUBSCRIBE, "")
                zmq_socket.setsockopt(zmq.CONFLATE, 1) # Берем только самый свежий кадр
                zmq_socket.connect(f"tcp://{ip}:5555")
                print(f"Подключено к ZMQ потоку: {ip}:5555")
            else:
                # Иначе это локальная камера
                cap = cv.VideoCapture(int(current_source))
                print(f"Открыта локальная камера: {current_source}")

        # ПОЛУЧЕНИЕ КАДРА
        frame_rgb = None

        if zmq_socket:
            try:
                # Получаем байты из ZMQ (не блокируем поток надолго)
                message = zmq_socket.recv(flags=zmq.NOBLOCK)
                frame = cv.imdecode(np.frombuffer(message, dtype=np.uint8), cv.IMREAD_COLOR)
                if frame is not None:
                    frame_rgb = cv.cvtColor(frame, cv.COLOR_BGR2RGB)
            except zmq.Again:
                pass # Кадр еще не прилетел

        elif cap:
            ret, frame = cap.read()
            if ret:
                frame_rgb = cv.cvtColor(frame, cv.COLOR_BGR2RGB)

        # ОТПРАВКА В ИНТЕРФЕЙС
        if frame_rgb is not None:
            unikostyl.main_loop_frame(frame_rgb)
        else:
            # Чтобы Pygame не зависал, если нет кадров, вызываем пустой цикл
            unikostyl.main_loop_frame(None)

if __name__ == "__main__":
    main_loop()
