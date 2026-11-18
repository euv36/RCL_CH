import unikostyl
import cv2 as cv
import numpy as np
import zmq
import json
import io
import time
from PIL import Image
from fisheye import open_camera, find_full_fov_camera
import cam_settings  # Idea 1: окно настроек камеры (кнопка CAM SET)
import gate_store  # Idea 3: сохранение/восстановление цветов ворот (saved_gates.json)

try:
    # В этой копии fisheye-обработки нет: оставляем заглушку, чтобы не падать.
    from fisheye import FisheyeProcessor
except Exception:
    FisheyeProcessor = None

try:
    # Idea 2: разбор JSON-статуса из multipart-видео 5555 [JPEG, JSON].
    from status_panel import parse_status_payload
except Exception:
    parse_status_payload = None


def build_gate_payload(pending_gates, roles=None):
    """
    Преобразует {1: (r,g,b), 2: (r,g,b)} в JSON-совместимый словарь
    для Raspberry Pi. Старое поле "rgb" добавляется только для ворот 1.

    Idea 9: roles — {gate_id: "ours"|"opponent"} (gate_store.roles_map);
    поле "role" добавляется ТОЛЬКО когда роль задана (additive-расширение
    контракта 5557: старый Pi неизвестные поля игнорирует, отсутствие
    поля = «роли нет» — роль сбросить проводом нельзя по схеме).
    """
    gates_payload = []
    legacy_rgb = None
    for gate_id in sorted(pending_gates):
        rgb = pending_gates[gate_id]
        r, g, b = int(rgb[0]), int(rgb[1]), int(rgb[2])
        item = {"id": int(gate_id), "rgb": [r, g, b]}
        if roles:
            role = roles.get(gate_id) or roles.get(str(gate_id))
            if role in ("ours", "opponent"):
                item["role"] = role
        gates_payload.append(item)
        if gate_id == 1:
            legacy_rgb = [r, g, b]

    payload_obj = {"gates": gates_payload}
    if legacy_rgb is not None:
        payload_obj["rgb"] = legacy_rgb
    return payload_obj


def main_loop():
    print("[SYSTEM] Запуск сетевого узла ноутбука...")

    zmq_context = zmq.Context()
    video_sub = None
    color_pub = zmq_context.socket(zmq.PUB)
    try:
        color_pub.bind("tcp://*:5557")
        # Idea 2: индикация статуса публикации цветов на панели.
        unikostyl.set_publish_state(True, "bound")
        pub_bound = True
    except zmq.ZMQError as e:
        # Порт занят (например, второй cv_kostyl) — UI живёт, но цвета
        # до Pi не дойдут: панель честно покажет ERR.
        unikostyl.set_publish_state(False, str(e))
        print(f"[SYSTEM] Не удалось занять порт 5557: {e}")
        pub_bound = False

    # Idea 3: восстановление сохранённых ворот из saved_gates.json.
    # Слоты 1..3 <- LAB-пороги (если они сохранены); цвета ворот, у
    # которых есть rgb, автоматически отправляются на Pi ниже.
    gate_store_obj = gate_store.GateStore()  # min_interval=0: пишет сразу
    restored_gates = {}
    for gate_id, rgb in gate_store_obj.restore_rgb():
        restored_gates[gate_id] = rgb
        lab = gate_store_obj.get_lab(gate_id)
        if lab is not None and gate_store.lab_configured(lab):
            unikostyl.thresholds[gate_id - 1] = list(lab)
    if restored_gates:
        # ВАЖНО: main_loop_frame каждый кадр переливает значения слайдеров
        # в thresholds[thr_index], поэтому слайдеры надо синхронизировать,
        # иначе восстановленный слот 1 затрётся стоковыми значениями.
        unikostyl.set_thr_to_sliders()
        print(
            "[SYSTEM] saved_gates.json: восстановлены ворота "
            f"{sorted(restored_gates)}"
        )

    # Idea 9: бейджи ролей в панели статуса (локальная таблица). Роли живут
    # в saved_gates.json (слоты 1..3); для Pi-источника роль приходит в
    # статус-JSON отдельным полем "role" у ворот. Обновляем при старте и
    # после каждой смены роли в окне CAM SET.
    def push_roles_to_panel():
        panel = unikostyl.widgets.get("status_panel")
        if panel is not None:
            panel.set_roles(gate_store_obj.roles())

    push_roles_to_panel()
    # Автоотправка восстановленных цветов: PUB-сообщения, отправленные до
    # подключения SUB-сокета Pi, ZMQ теряет. Поэтому шлём сразу и повторяем
    # первые RESTORED_SEND_WINDOW секунд (и заново при каждом CONNECT к Pi).
    RESTORED_SEND_WINDOW = 10.0
    RESTORED_SEND_INTERVAL = 2.0
    restored_send_until = time.monotonic() + RESTORED_SEND_WINDOW
    next_restored_send = time.monotonic()

    # Idea 1 (fix): ELP — та камера, что отдаёт полный FOV 1920x1080
    # (встроенная вебка 1080p не умеет). Режимы ELP ниже 1080p — сенсорный
    # КРОП (720p = центр 66.7%), FOV теряется, поэтому открываем именно
    # 1080p, а уменьшение делаем программно в кадре (ниже), никогда
    # сменой режима камеры.
    local_idx = find_full_fov_camera()
    if local_idx is None:
        local_idx = 1  # известный запасной индекс ELP на этом ноутбуке
        print(f"[SYSTEM] Камера с полным FOV не найдена, запасной индекс: {local_idx}")
    else:
        print(f"[SYSTEM] Локальная камера с полным FOV: индекс {local_idx}")
    cap = open_camera(local_idx, 1920, 1080, 30)
    print(
        f"[SYSTEM] Локальная камера {local_idx}: "
        f"{int(cap.get(cv.CAP_PROP_FRAME_WIDTH))}x"
        f"{int(cap.get(cv.CAP_PROP_FRAME_HEIGHT))}"
    )
    # Idea 2: индикация источника видео на панели; поле Camera Source
    # показываем с найденным индексом — видно, какая камера открыта.
    unikostyl.set_video_source("local", str(local_idx))
    unikostyl.camera_input_text = str(local_idx)
    fisheye_proc = FisheyeProcessor() if FisheyeProcessor else None
    if FisheyeProcessor is None:
        print(
            "[SYSTEM] fisheye-обработка недоступна в этой копии — кадры идут как есть"
        )
    print(
        "[SYSTEM] Ноутбук готов. Введите IP Pi в поле 'Camera Source' и нажмите CONNECT."
    )

    # Idea 1: окно настроек камеры. Кнопка CAM SET появляется в главном UI,
    # каждый кадр нужно один вызов cam_ui.pump(). pi_target обновляется
    # при подключении к Pi-стриму (бэкенд окна следует за источником).
    pi_target = None
    cam_ui = cam_settings.install(
        unikostyl,
        cap_getter=lambda: cap,
        source_getter=lambda: ("pi", pi_target) if video_sub is not None
        else ("local", None),
        # Idea 9: селектор ролей G1..G3 в окне CAM SET пишет в тот же
        # gate_store (один владелец saved_gates.json на процесс).
        gate_store_obj=gate_store_obj,
        roles_changed_cb=push_roles_to_panel,
    )

    # Диагностика FPS (запрос пользователя «очень медленно»): посекундные
    # тайминги стадий — видно, какая стадия съедает кадр (cap=чтение+декод
    # MSMF, pre=поворот+ресайз+RGB, ui=main_loop_frame, pump=cv2-окна).
    _stage = {"cap": 0.0, "pre": 0.0, "ui": 0.0, "pump": 0.0}
    _stage_n = 0
    _stage_t0 = time.monotonic()

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
                pi_target = src  # Idea 1: цель для отправки настроек камеры
                # Idea 3: Pi мог перезапуститься и потерять цвета — заново
                # автоотправляем восстановленные слоты новому подписчику.
                restored_send_until = time.monotonic() + RESTORED_SEND_WINDOW
                next_restored_send = time.monotonic()
                unikostyl.set_video_source("pi", src)
                print(f"[SYSTEM] Подключено к видео Pi: {src}")
            else:
                # Idea 1 (fix): локальные камеры тоже открываем с запросом
                # 1920x1080 (MSMF отдаст максимум доступного — факт в логе).
                cap = open_camera(int(src), 1920, 1080, 30)
                unikostyl.set_video_source("local", src)
                print(f"[SYSTEM] Подключено к локальной камере: {src}")
                print(
                    f"[SYSTEM] Локальная камера {src}: "
                    f"{int(cap.get(cv.CAP_PROP_FRAME_WIDTH))}x"
                    f"{int(cap.get(cv.CAP_PROP_FRAME_HEIGHT))}"
                )

        # Получение кадра в RGB
        frame_rgb = None
        if video_sub:
            try:
                # Idea 2: Pi шлёт либо одиночный JPEG (старый протокол), либо
                # multipart [JPEG, JSON-статус] — принимаем оба. Status ставим
                # ДО декодирования: пусть даже битый кадр не теряет статус.
                parts = video_sub.recv_multipart(flags=zmq.NOBLOCK)
                if len(parts) > 1 and parse_status_payload is not None:
                    status = parse_status_payload(parts[1])
                    if status is not None:
                        unikostyl.set_pi_status(status)
                # JPEG декодируем сразу в RGB через PIL – никаких BGR!
                image = Image.open(io.BytesIO(parts[0]))
                frame_rgb = np.array(image.convert("RGB"))
                # При необходимости можно повернуть:
                # frame_rgb = np.rot90(frame_rgb, k=1)  # пример поворота на 90°
            except zmq.Again:
                pass
            except Exception as e:
                print(f"[NET ERROR] {e}")
        elif cap and cap.isOpened():
            _t = time.perf_counter()
            ret, f = cap.read()
            _stage["cap"] += time.perf_counter() - _t
            if ret:
                _t = time.perf_counter()
                # Idea 1 (fix): захват 1920x1080 (полный FOV ELP). Рабочий
                # размер пайплайна прежний — 720x1280 после поворота.
                # Оптимизация FPS: сначала INTER_AREA-ресайз (дорогая часть
                # обрабатывает тот же источник, но поворот и BGR->RGB потом
                # идут по в 2.7 раза меньшему кадру), затем поворот.
                if f.shape[0] != 1080 or f.shape[1] != 1280:
                    f = cv.resize(f, (1280, 720), interpolation=cv.INTER_AREA)
                f = cv.rotate(f, cv.ROTATE_90_COUNTERCLOCKWISE)
                # Локальная веб-камера отдаёт BGR, конвертируем в RGB
                frame_rgb = cv.cvtColor(f, cv.COLOR_BGR2RGB)
                _stage["pre"] += time.perf_counter() - _t

        if frame_rgb is not None and unikostyl.fisheye_mode and fisheye_proc:
            mode = "ring" if unikostyl.fisheye_mode == 2 else "stretch"
            frame_bgr = cv.cvtColor(frame_rgb, cv.COLOR_RGB2BGR)
            frame_bgr = fisheye_proc.process(frame_bgr, mode=mode)
            frame_rgb = cv.cvtColor(frame_bgr, cv.COLOR_BGR2RGB)

        # Запуск интерфейса (передаем чистый RGB кадр).
        # Второй элемент — словарь обновлений цветов ворот: {1: (r,g,b), 2: ...}.
        _t = time.perf_counter()
        _, pending_gates = unikostyl.main_loop_frame(frame_rgb)
        _stage["ui"] += time.perf_counter() - _t

        # Idea 3: автоотправка восстановленных цветов на Pi (первые
        # RESTORED_SEND_WINDOW секунд после старта/CONNECT, каждые
        # RESTORED_SEND_INTERVAL секунд — пока SUB Pi подключится).
        now_mono = time.monotonic()
        if (
            restored_gates
            and pub_bound
            and now_mono >= next_restored_send
            and now_mono < restored_send_until
        ):
            # Idea 9: автоотправка несёт и роли (только заданные, поле
            # "role" — additive; unset в провод не попадает никогда).
            payload = json.dumps(
                build_gate_payload(
                    restored_gates, gate_store_obj.roles()
                )
            )
            color_pub.send_string(payload)
            unikostyl.note_colors_sent()
            print(f"[NETWORK] --> Восстановленные цвета ворот: {payload}")
            next_restored_send = now_mono + RESTORED_SEND_INTERVAL

        # Если нажали SAVE G1..G3 – отправляем обновления на Pi.
        # Формат payload включает все сохранённые ворота (1..3).
        if pending_gates:
            # Idea 9: SAVE несёт роль слота (если задана в CAM SET; снимается
            # роль только в самом CAM SET — «unset» в проводе не передаётся).
            payload = json.dumps(
                build_gate_payload(
                    pending_gates, gate_store_obj.roles()
                )
            )
            color_pub.send_string(payload)
            unikostyl.note_colors_sent()
            print(f"[NETWORK] --> Отправлены цвета ворот: {payload}")
            # Idea 3: немедленная атомарная запись в saved_gates.json —
            # новый цвет слота n заменяет предыдущий; остальные слоты
            # (уже сохранённые) остаются в файле как были (upsert).
            for gate_id in sorted(pending_gates):
                if not (1 <= gate_id <= 3):
                    continue
                gate_store_obj.set_rgb(
                    gate_id,
                    pending_gates[gate_id],
                    lab=list(map(int, unikostyl.thresholds[gate_id - 1])),
                )
            print(
                f"[SYSTEM] saved_gates.json обновлён: слоты {sorted(pending_gates)}"
            )

        _t = time.perf_counter()
        cam_ui.pump()
        # Idea 7 (запрос пользователя): отдельное ресайз-окно RADAR;
        # waitKey делает то окно, чей cv2-конкурент открыт (не двоим).
        radar_panel_mod = unikostyl.widgets.get("radar_panel")
        if radar_panel_mod is not None:
            radar_panel_mod.pump_window(do_waitkey=not cam_ui.is_open())
        _stage["pump"] += time.perf_counter() - _t
        _stage_n += 1
        if time.monotonic() - _stage_t0 >= 5.0 and _stage_n:
            _fps = _stage_n / (time.monotonic() - _stage_t0)
            print(f"[FPS] ui {_fps:.1f} fps | cap {_stage['cap'] / _stage_n * 1000:.1f} "
                  f"pre {_stage['pre'] / _stage_n * 1000:.1f} "
                  f"ui {_stage['ui'] / _stage_n * 1000:.1f} "
                  f"pump {_stage['pump'] / _stage_n * 1000:.1f} ms/frame")
            _stage = {"cap": 0.0, "pre": 0.0, "ui": 0.0, "pump": 0.0}
            _stage_n = 0
            _stage_t0 = time.monotonic()

    if cap:
        cap.release()
    if video_sub:
        video_sub.close()
    cam_ui.close()  # Idea 1: сохранить настройки камеры при выходе
    _rp = unikostyl.widgets.get("radar_panel")
    if _rp is not None:
        _rp.close_window()  # Idea 7: окно радара
    color_pub.close()
    zmq_context.term()


if __name__ == "__main__":
    main_loop()
