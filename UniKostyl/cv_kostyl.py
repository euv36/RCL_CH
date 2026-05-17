import unikostyl
import cv2 as cv
import numpy as np
import urllib.request
import time

# URL твоей Raspberry Pi
URL = "http://10.70.189.51:8000/"

def main_loop():
    print(f"Попытка подключения к Raspberry Pi: {URL}")

    stream = None
    while stream is None:
        try:
            stream = urllib.request.urlopen(URL, timeout=5)
            print("Соединение установлено!")
        except Exception as e:
            print(f"Не удалось подключиться ({e}). Повтор через 2 секунды...")
            time.sleep(2)

    bytes_buffer = b""

    while True:
        try:
            # Читаем чанк данных из сети
            bytes_buffer += stream.read(4096)

            # Ищем начало и конец JPEG кадра
            a = bytes_buffer.find(b'\xff\xd8') # Начало JPEG
            b = bytes_buffer.find(b'\xff\xd9') # Конец JPEG

            if a != -1 and b != -1:
                # Вырезаем только байты картинки
                jpg = bytes_buffer[a:b+2]
                # Оставляем остаток в буфере для следующего кадра
                bytes_buffer = bytes_buffer[b+2:]

                # Декодируем JPEG в массив NumPy
                frame = cv.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv.IMREAD_COLOR)

                if frame is not None:
                    # ВАЖНО: Твоя малина шлет кадр, на котором уже нарисованы синие рамки.
                    # OpenCV читает в BGR, UniKostyl ждет RGB.
                    frame_rgb = cv.cvtColor(frame, cv.COLOR_BGR2RGB)

                    # Передаем в интерфейс
                    unikostyl.main_loop_frame(frame_rgb)
        except Exception as e:
            print(f"Ошибка потока: {e}")
            # В случае обрыва — пробуем переподключиться
            time.sleep(1)
            try:
                stream = urllib.request.urlopen(URL, timeout=5)
            except:
                pass

if __name__ == "__main__":
    main_loop()
