#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rpi_cam_ctl.py — мини-сервис настроек камеры на Raspberry Pi (Idea 1).

Топология — «как у цветового SUB» (PROMPT §4/§5): PUB БИНДИТСЯ на ноутбуке
(cv_kostyl.py / cam_settings.py, порт 5559), этот сервис делает SUB **connect**
к ноутбуку и слушает. Никакого GUI на Pi.

  ноутбук PUB bind  tcp://*:5559
  Pi      SUB connect  tcp://<laptop-ip>:5559

Сообщения (JSON в одном кадре ZMQ):
  {"cmd": "set", "controls": {"exposure": 300, "gain": 0, ...}, "ts": "..."}
  {"cmd": "get"}                                — залогировать текущие значения
Неизвестные команды/имена контролов игнорируются с WARN (не падаем).
Значения клампятся к железно-проверенным диапазонам (UniKostyl/AGENTS.md).
АВТОЭКСПОЗИЦИЯ НИКОГДА НЕ ВКЛЮЧАЕТСЯ: auto_exposure всегда принудительно 1
(ручной режим), поле "auto_exposure" из входного JSON отбрасывается.

При старте применяет camera_settings.json (рядом со скриптом), если файл есть:
тот же контракт, что и в rpi_elp_cam.py (файл применяется ПОВЕРХ значений по
умолчанию). Приложение обычно запущено (камера занята OpenCV), но v4l2-ctl
меняет контролы и во время стрима — это железно проверено.

Запуск:
  python3 rpi_cam_ctl.py --laptop-ip <ip-ноутбука>
Зависимости: python3-zmq, v4l-utils. Только для Pi (Linux).
"""

import argparse
import json
import os
import subprocess
import sys
import time

try:
    import zmq
except ImportError:
    print("Нет pyzmq. Установите: sudo apt install python3-zmq (или pip install pyzmq)")
    raise

# Папка, где лежит скрипт (=/home/twox при деплое по инструкции).
_HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_LAPTOP_IP = "10.192.226.77"   # совпадает с rpi_elp_cam.py; IP плавает — проверяйте arp -a
DEFAULT_PORT = 5559
DEFAULT_DEVICE = "/dev/video0"

# ===================== РЕЕСТР КОНТРОЛОВ =====================
# Копия реестра из UniKostyl/cam_settings.py (файл разворачивается на Pi
# отдельно и не импортирует модули ноутбука). ПРИ ИЗМЕНЕНИИ — СИНХРОНИЗИРОВАТЬ.
# Диапазоны железно-проверены: UniKostyl/AGENTS.md «Camera facts».
CONTROLS = {
    "exposure":                  ("exposure_time_absolute",    1, 5000, 300),
    "gain":                      ("gain",                      0,  100, 0),
    "brightness":                ("brightness",              -64,   64, 0),
    "gamma":                     ("gamma",                    72,  500, 100),
    "saturation":                ("saturation",                0,  128, 60),
    "contrast":                  ("contrast",                  0,   64, 32),
    "power_line_frequency":      ("power_line_frequency",      0,    2, 1),   # 1 = 50 Гц
    "exposure_dynamic_framerate": ("exposure_dynamic_framerate", 0,  1, 0),  # off
}
SAFE_DEFAULTS = {name: spec[3] for name, spec in CONTROLS.items()}


def log(msg, level="INFO"):
    print(f"[{time.strftime('%H:%M:%S')}] [{level}] {msg}", flush=True)


def clamp(name, value):
    spec = CONTROLS.get(name)
    if spec is None:
        raise KeyError(name)
    try:
        v = int(value)
    except (TypeError, ValueError):
        v = spec[3]
    return max(spec[1], min(spec[2], v))


# ===================== V4L2 =====================
# Тот же паттерн, что v4l2_set в rpi_elp_cam.py: сначала одна пачка,
# при отказе — повтор по одному контролю (один неактивный контрол раньше
# молча ронял всю пачку, и gamma/saturation/contrast не применялись).
def _v4l2_run(args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=3)
    except FileNotFoundError:
        log("v4l2-ctl не найден; установите v4l-utils (sudo apt install v4l-utils)", "WARN")
        return None
    except subprocess.TimeoutExpired:
        log("v4l2-ctl не ответил за 3 с", "WARN")
        return None


def v4l2_set(device, **controls):
    """Установить контролы через v4l2-ctl. Ошибки не фатальны, но логируются."""
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
        single = _v4l2_run(["v4l2-ctl", "-d", device, "-c", f"{name}={int(value)}"])
        if single is None:
            return False
        if single.returncode == 0:
            ok_any = True
        else:
            err = (single.stderr or single.stdout).strip().replace("\n", " ")
            log(f"Контрол {name}={value} не применён ({err})", "WARN")
    return ok_any


def v4l2_get(device, names):
    """Текущие значения контролов: v4l2-ctl -d <dev> --get-ctrl=a,b,c."""
    if not names:
        return {}
    res = _v4l2_run(["v4l2-ctl", "-d", device, f"--get-ctrl={','.join(names)}"])
    if res is None or res.returncode != 0:
        return {}
    out = {}
    for line in (res.stdout or "").splitlines():
        if ":" in line:
            key, _, val = line.partition(":")
            out[key.strip()] = val.strip()
    return out


# ===================== ПРИМЕНЕНИЕ =====================
def apply_controls(device, controls, source="msg"):
    """Отфильтровать/клампить контролы и применить. Возвращает применённый dict.

    "auto_exposure" из входного JSON всегда отбрасывается и принудительно
    ставится 1 (ручной режим) — автоэкспозицию запрещено включать.
    """
    valid = {}
    for name, value in (controls or {}).items():
        if name in CONTROLS:
            try:
                valid[name] = clamp(name, value)
            except Exception as e:
                log(f"Контрол {name}={value!r} отброшен: {e}", "WARN")
        else:
            log(f"Неизвестный контрол {name!r} проигнорирован", "WARN")

    kwargs = {CONTROLS[name][0]: value for name, value in valid.items()}
    kwargs["auto_exposure"] = 1  # ручная экспозиция, AE не включаем никогда
    ok = v4l2_set(device, **kwargs)
    if ok:
        log(f"Применено ({source}): {valid}")
    else:
        log(f"Не удалось применить контролы ({source}): {valid}", "WARN")
    return valid


def apply_settings_file(device, path):
    """camera_settings.json при старте.

    Файла нет или он повреждён -> НИЧЕГО не применяем: отсутствие файла
    означает «сохранённых настроек нет», а не «сбросить в дефолты» (иначе
    сервис затёр бы CLI-флаги rpi_elp_cam.py при позднем старте).
    Повреждённый JSON не роняет сервис.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        log(f"{path} не найден — работает то, что выставлено CLI-флагами")
        return None
    except Exception as e:
        log(f"{path} повреждён ({e}) — пропускаю авто-применение", "WARN")
        return None

    controls = data.get("controls") if isinstance(data, dict) else None
    if not isinstance(controls, dict):
        log(f"{path}: нет секции controls — пропускаю авто-применение", "WARN")
        return None
    log(f"{path}: {controls}")
    return apply_controls(device, controls, source="camera_settings.json")


# ===================== ОСНОВНОЙ ЦИКЛ =====================
def main():
    ap = argparse.ArgumentParser(
        description="Мини-сервис настроек камеры на Pi (SUB connect к ноутбуку :5559)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--laptop-ip", default=DEFAULT_LAPTOP_IP,
                    help="IP ноутбука (PUB :5559 биндится там; IP плавает — см. arp -a)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--device", default=DEFAULT_DEVICE)
    ap.add_argument("--settings-file",
                    default=os.path.join(_HERE, "camera_settings.json"))
    ap.add_argument("--no-apply-on-start", action="store_true",
                    help="не применять настройки при старте")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    ctx = zmq.Context()
    sub = ctx.socket(zmq.SUB)
    sub.setsockopt_string(zmq.SUBSCRIBE, "")
    sub.setsockopt(zmq.CONFLATE, 1)  # актуален только последний набор
    sub.setsockopt(zmq.LINGER, 0)
    endpoint = f"tcp://{args.laptop_ip}:{args.port}"
    sub.connect(endpoint)
    log(f"SUB connect {endpoint} (жду PUB ноутбука; сообщения до подключения "
        "PUB теряются — окно настроек повторяет набор каждые 5 с)")

    if not args.no_apply_on_start:
        apply_settings_file(args.device, args.settings_file)

    log("Сервис запущен. Ctrl+C — выход.")
    try:
        while True:
            try:
                raw = sub.recv_string(flags=zmq.NOBLOCK)
            except zmq.Again:
                time.sleep(0.05)  # 20 Гц опроса достаточно, без busy-wait
                continue
            except zmq.ZMQError as e:
                log(f"ZMQ: {e}; переподключение через 1 с", "WARN")
                time.sleep(1.0)
                continue

            try:
                msg = json.loads(raw)
                if not isinstance(msg, dict):
                    raise ValueError("не объект JSON")
            except Exception as e:
                log(f"Битое сообщение отброшено: {e!r} "
                    f"({raw[:80]!r}...)", "WARN")
                continue

            cmd = msg.get("cmd", "set")
            if cmd == "set":
                apply_controls(args.device, msg.get("controls"), source="msg")
            elif cmd == "get":
                names = [spec[0] for spec in CONTROLS.values()]
                log(f"Текущие контролы: {v4l2_get(args.device, names)}")
            else:
                log(f"Неизвестная команда {cmd!r} проигнорирована", "WARN")
    except KeyboardInterrupt:
        log("Остановка по Ctrl+C")
    finally:
        sub.close(0)
        try:
            ctx.term()
        except Exception:
            pass
        log("Завершено")


if __name__ == "__main__":
    main()
