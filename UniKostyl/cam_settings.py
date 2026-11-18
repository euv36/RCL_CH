# -*- coding: utf-8 -*-
"""
cam_settings.py — Idea 1 (PROMPT_EN.md §5): окно настроек камеры.

Что здесь есть:
  * Реестр контролов с железно-проверенными V4L2-диапазонами (UniKostyl/AGENTS.md,
    «Camera facts»): exposure 1..5000, gain 0..100, brightness −64..64,
    gamma 72..500, saturation 0..128, contrast 0..64,
    power_line_frequency 0..2 (1 = 50 Гц), exposure_dynamic_framerate 0/1.
  * Атомарное чтение/запись camera_settings.json (tmp + os.replace + fsync).
    Неизвестные верхнеуровневые ключи файла СОХРАНЯЮТСЯ (секции будущих идей,
    например self_mask из Idea 8, не затираются).
  * Локальный бэкенд: применение через cv2.CAP_PROP_* на открытый VideoCapture
    + обязательное чтение обратно (MSMF врёт о применённых значениях).
    Неподдерживаемый контрол честно помечается «n/a», не молча.
  * Бэкенд Pi: ZMQ PUB, БИНДИТСЯ ноутбуком на tcp://*:5559; мини-сервис
    rpi_cam_ctl.py на Pi делает SUB connect к ноутбуку — ровно тот же
    паттерн, что у цветового SUB (5557). Формат сообщения:
      {"cmd": "set", "controls": {"exposure": 300, ...}, "ts": "..."}
    (в PROMPT написано «laptop PUB tcp://<pi>:5559», но одновременно и
    «SUB connect к ноутбуку, как у цветового SUB» — это конфликтует;
    принят паттерн цветового SUB: SUB всегда connect, PUB всегда bind,
    т.е. 5559 слушает НОУТБУК, Pi подключается к tcp://<laptop-ip>:5559).
  * Окно настроек на cv2-трекбарах (отдельное сущность, fisheye-трекбары
    не трогаем). Открывается ТОЛЬКО кнопкой CAM SET в главном UI.
    Главный цикл должен звать window.pump() раз в кадр, пока окно открыто.
  * Idea 9: секция GATE ROLES — роль слота 1..3 ("ours"/"opponent"/"unset")
    кнопками на полотне. Роль живёт в saved_gates.json (gate_store), НЕ в
    camera_settings.json; пишется сразу при клике (atomic), колбэк
    roles_changed_cb уведомляет UI (бейджи панели статуса).
  * install() — подключение к cv_kostyl.py: одна кнопка + один вызов pump().

Правила (PROMPT §5 Idea 1):
  * Автоэкспозиция НИКОГДА не включается: параметр auto_exposure отсутствует
    в UI и в JSON, Pi-сервис всегда принудительно ставит auto_exposure=1
    (ручной режим). На Windows CAP_PROP_AUTO_EXPOSURE сознательно не трогаем:
    у бэкендов разные соглашения (0.25/0.75 против 1/3), риск случайно
    включить AE важнее сомнительного выигрыша.
  * Кнопка «SAFE DEFAULTS» — железно-проверенные значения: gain=0,
    brightness=0, exposure=300, gamma=100, saturation=60, contrast=32,
    power_line=1 (50 Гц), dynamic_framerate=0.
  * brightness>0 помечается предупреждением: вымывает цвета (HSV S 168→20).

Файл camera_settings.json: рядом с cv_kostyl.py (UniKostyl/) на ноутбуке,
/home/twox/camera_settings.json на Pi. Схема:
  {"version": 1, "ts": "...", "controls": {"exposure": 300, ...}}
"""

import json
import math
import os
import time

import cv2
import numpy as np
import pygame  # нужен только для кнопки в главном UI (см. install)

import gate_store  # Idea 9: роли ворот живут в saved_gates.json (stdlib-only)

try:
    import zmq
except ImportError:  # Pi-бэкенд опционален, локальный работает и без pyzmq
    zmq = None


# ===================== РЕЕСТР КОНТРОЛОВ =====================
# Диапазоны — железно-проверенные факты из UniKostyl/AGENTS.md.
# "v4l2"  — имя контрола для v4l2-ctl (Pi-сторона);
# "cap"   — cv2-константа CAP_PROP_* или None (нет прямого аналога в cv2);
# "default" — «безопасные» значения из verified-настроек.
# ВАЖНО: при изменении реестра синхронизировать копию в rpi_cam_ctl.py
# (он разворачивается на Pi отдельным файлом и не импортирует этот модуль).
CONTROLS = {
    "exposure": {
        "v4l2": "exposure_time_absolute", "min": 1, "max": 5000,
        "default": 300, "cap": cv2.CAP_PROP_EXPOSURE,
    },
    "gain": {
        "v4l2": "gain", "min": 0, "max": 100,
        "default": 0, "cap": cv2.CAP_PROP_GAIN,
    },
    "brightness": {
        "v4l2": "brightness", "min": -64, "max": 64,
        "default": 0, "cap": cv2.CAP_PROP_BRIGHTNESS,
    },
    "gamma": {
        "v4l2": "gamma", "min": 72, "max": 500,
        "default": 100, "cap": cv2.CAP_PROP_GAMMA,
    },
    "saturation": {
        "v4l2": "saturation", "min": 0, "max": 128,
        "default": 60, "cap": cv2.CAP_PROP_SATURATION,
    },
    "contrast": {
        "v4l2": "contrast", "min": 0, "max": 64,
        "default": 32, "cap": cv2.CAP_PROP_CONTRAST,
    },
    # 0 = выкл, 1 = 50 Гц, 2 = 60 Гц. Через cv2 на Windows недоступен.
    "power_line_frequency": {
        "v4l2": "power_line_frequency", "min": 0, "max": 2,
        "default": 1, "cap": None,
    },
    # V4L2-only: не «давить» FPS при долгой экспозиции. Держим 0 (off).
    "exposure_dynamic_framerate": {
        "v4l2": "exposure_dynamic_framerate", "min": 0, "max": 1,
        "default": 0, "cap": None,
    },
}

SAFE_DEFAULTS = {name: ctrl["default"] for name, ctrl in CONTROLS.items()}


def clamp(name, value):
    """Ограничить значение контрола его V4L2-диапазоном; исключения -> default."""
    ctrl = CONTROLS.get(name)
    if ctrl is None:
        raise KeyError(name)
    try:
        v = int(value)
    except (TypeError, ValueError):
        v = ctrl["default"]
    return max(ctrl["min"], min(ctrl["max"], v))


def readback_tolerance(name):
    """Допуск при сравнении «запросили -> камера отчиталась».

    ~0.2% диапазона, но не меньше 1: драйверы квантуют шаги по-разному,
    а MSMF может вообще не применить значение (тогда это не «погрешность»,
    а «n/a» — см. LocalCameraCtl.apply).
    """
    ctrl = CONTROLS[name]
    return max(1, (ctrl["max"] - ctrl["min"]) // 500)


def default_settings_path():
    """Файл настроек рядом с cv_kostyl.py (каталог этого модуля = UniKostyl/)."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "camera_settings.json")


# ===================== JSON (атомарно, обе стороны) =====================
def save_settings(path, controls, extra_top=None):
    """Атомарно вписать controls в camera_settings.json.

    Существующий файл читается, неизвестные ключи/секции сохраняются
    (файл общий с будущими идеями — self_mask и т.п.). Запись: tmp-файл
    в том же каталоге -> fsync -> os.replace (атомарно на POSIX и Windows).
    """
    data = {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
        if isinstance(loaded, dict):
            data = loaded
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"[cam_settings] {path}: не прочитан (перезапишу целиком): {e}")
        data = {}

    ctrl = data.get("controls")
    if not isinstance(ctrl, dict):
        ctrl = {}
    for name, value in controls.items():
        if name in CONTROLS:  # чужие имена не пишем
            ctrl[name] = clamp(name, value)
    data["controls"] = ctrl
    data["version"] = 1
    data["ts"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    if extra_top:
        data.update(extra_top)

    directory = os.path.dirname(path) or "."
    os.makedirs(directory, exist_ok=True)
    tmp = os.path.join(directory, f".camera_settings.tmp{os.getpid()}")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False, sort_keys=True)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    return data


def load_settings(path):
    """Прочитать controls из camera_settings.json.

    Возвращает dict {имя: значение} (только известные имена, с клампом)
    или None, если файла нет / он повреждён. Повреждённый файл НЕ роняет
    вызывающего (робот не должен «сходить с ума» из-за JSON).
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except Exception as e:
        print(f"[cam_settings] {path}: повреждён, использую значения по умолчанию: {e}")
        return None
    if not isinstance(data, dict):
        return None
    ctrl = data.get("controls")
    if not isinstance(ctrl, dict):
        return None
    out = {}
    for name in CONTROLS:
        if name in ctrl:
            try:
                out[name] = clamp(name, ctrl[name])
            except Exception:
                pass
    return out


# ===================== ЛОКАЛЬНЫЙ БЭКЕНД (cv2.CAP_PROP_*) =====================
class LocalCameraCtl:
    """Применение контролов к локальному VideoCapture + чтение обратно.

    Железно-проверено на ELP через MSMF (Windows), 2026-11-17:
      * gain/gamma/saturation/contrast — set+get работают честно (ok);
      * brightness — set РАБОТАЕТ (картинка меняется, размах mean gray
        51->162), но get() всегда возвращает 0.0;
      * exposure — set работает, но В ЕДИНИЦАХ log2 секунд (конвенция
        DirectShow/MSMF; диапазон UVC 1..5000 по 100 мкс == log2 -13.3..-1),
        а get() всегда возвращает -6.0;
      * power_line_frequency / exposure_dynamic_framerate — CAP_PROP нет.
    Поэтому:
      * exposure на MSMF конвертируется log2(uvc*1e-4) — значения в
        camera_settings.json везде хранятся в UVC-единицах (как на Pi);
      * «залипшая» читка детектится динамически (3 разных запроса ->
        одно и то же ответное значение) и помечается на сессию: статус
        unconfirmed («применено, но не подтверждено — смотрите картинку»),
        НЕ ok и НЕ молча.

    Состояния:
      ok          — камера подтвердила значение (в допуске);
      unconfirmed — set прошёл, но читка бэкенда «залипла»/в других единицах;
      mismatch    — камера отчиталась другим правдоподобным значением;
      n/a         — контрол не поддержан бэкендом (нет CAP_PROP / читка
                    0/-1 при ненулевом запросе);
      nocam       — камера не открыта;
      fail        — cv2.error при set/get.
    """

    def __init__(self, cap_getter):
        self.cap_getter = cap_getter    # callable -> cv2.VideoCapture | None
        self._backend = None            # имя бэкенда (MSMF/V4L2/...), лениво
        self._rb_hist = {}              # name -> [(requested, readback), ...]
        self._unreliable = set()        # контролы с «залипшей» читкой

    def _cap(self):
        try:
            cap = self.cap_getter()
        except Exception:
            return None
        if cap is None:
            return None
        try:
            if not cap.isOpened():
                return None
        except Exception:
            return None
        return cap

    def _backend_name(self, cap):
        if self._backend is None:
            try:
                self._backend = cap.getBackendName() or ""
            except Exception:
                self._backend = ""
        return self._backend

    def _write_value(self, cap, name, value):
        """Значение для cap.set с поправкой на единицы бэкенда.

        MSMF/DirectShow трактует CAP_PROP_EXPOSURE как log2 секунд; UVC-единица
        (100 мкс) -> log2: 300 -> -5.06 (≈30 мс), 1..5000 -> -13.3..-1.
        На V4L2 (Pi/Linux) CAP_PROP_EXPOSURE — те же 100-мкс единицы, что и в
        JSON, конверсия не нужна.
        """
        if name == "exposure" and self._backend_name(cap) == "MSMF":
            return math.log2(max(value, 1) * 1e-4)
        return float(value)

    def _note_readback(self, name, value, rb_val):
        """Накопить историю (запрос -> читка) и пометить «залипшие» контролы."""
        hist = self._rb_hist.setdefault(name, [])
        hist.append((value, None if rb_val is None else round(rb_val, 3)))
        if len(hist) > 12:
            del hist[:-12]
        reqs = {r for r, _ in hist}
        rbs = {b for _, b in hist}
        if len(reqs) >= 3 and len(rbs) == 1:
            self._unreliable.add(name)
        elif len(rbs) > 1:
            self._unreliable.discard(name)

    def apply(self, controls):
        """Применить подмножество контролов; вернуть {имя: статус}."""
        cap = self._cap()
        results = {}
        for name, value in controls.items():
            ctrl = CONTROLS.get(name)
            if ctrl is None:
                continue
            value = clamp(name, value)
            if cap is None:
                results[name] = {"requested": value, "readback": None,
                                 "state": "nocam", "note": "камера не открыта"}
                continue
            if ctrl["cap"] is None:
                results[name] = {"requested": value, "readback": None,
                                 "state": "n/a",
                                 "note": "нет CAP_PROP_* в cv2 (V4L2-only; "
                                         "настройте через Pi)"}
                continue
            try:
                cap.set(ctrl["cap"], self._write_value(cap, name, value))
                rb = cap.get(ctrl["cap"])
            except cv2.error as e:
                results[name] = {"requested": value, "readback": None,
                                 "state": "fail", "note": str(e)[:60]}
                continue
            rb_val = None if rb is None else float(rb)

            if name == "exposure" and self._backend_name(cap) == "MSMF":
                # Читка в log2 с и «залипшая»: сравнивать с UVC нельзя.
                rb_uvc = 2 ** rb_val * 1e4 if rb_val is not None else None
                self._note_readback(name, value, rb_val)
                results[name] = {
                    "requested": value, "readback": rb_val, "state": "unconfirmed",
                    "note": (f"MSMF: читка log2 c ({rb_val:.1f} ≈ "
                             f"{rb_uvc:.0f} UVC), запрос {value} UVC; "
                             "подтверждения нет — смотрите картинку"),
                }
                continue

            self._note_readback(name, value, rb_val)
            if name in self._unreliable:
                results[name] = {"requested": value, "readback": rb_val,
                                 "state": "unconfirmed",
                                 "note": (f"читка бэкенда не меняется "
                                          f"(всегда {rb_val}); применено "
                                          "без подтверждения — смотрите "
                                          "картинку")}
            elif rb_val is not None and abs(rb_val - value) <= readback_tolerance(name):
                results[name] = {"requested": value, "readback": rb_val,
                                 "state": "ok", "note": ""}
            elif rb_val is not None and rb_val in (0.0, -1.0) and value != 0:
                results[name] = {"requested": value, "readback": rb_val,
                                 "state": "n/a",
                                 "note": "не подтверждено (читка 0/-1)"}
            else:
                results[name] = {"requested": value, "readback": rb_val,
                                 "state": "mismatch",
                                 "note": "камера отчиталась другим значением"}
        return results

    def read_current(self):
        """Текущие значения поддерживаемых контролов (для инициализации UI).

        На бэкендах с «залипшей» читкой (MSMF: brightness=0, exposure=-6)
        значения ненадёжны — их нельзя показывать как факт; для exposure на
        MSMF читка вообще в других единицах. Поэтому read_current пропускает
        контролы, помеченные ненадёжными, и exposure на MSMF.
        """
        cap = self._cap()
        out = {}
        if cap is None:
            return out
        msmf = self._backend_name(cap) == "MSMF"
        for name, ctrl in CONTROLS.items():
            if ctrl["cap"] is None or name in self._unreliable:
                continue
            if name == "exposure" and msmf:
                continue  # единицы log2 c, читка залипла
            try:
                v = float(cap.get(ctrl["cap"]))
            except Exception:
                continue
            if ctrl["min"] <= v <= ctrl["max"]:
                out[name] = int(round(v))
        return out


# ===================== PI-БЭКЕНД (ZMQ PUB :5559) =====================
def build_pi_message(controls, cmd="set"):
    """JSON-сообщение для rpi_cam_ctl.py. Чужие имена отбрасываются,
    значения клампятся — на Pi не должно уезжать ничего невалидного."""
    if cmd not in ("set", "get"):
        raise ValueError(cmd)
    msg = {"cmd": cmd, "ts": time.strftime("%Y-%m-%dT%H:%M:%S")}
    if cmd == "set":
        msg["controls"] = {n: clamp(n, v) for n, v in controls.items()
                           if n in CONTROLS}
    return json.dumps(msg)


class PiCamCtl:
    """PUB-сокет, БИНДЯЩИЙСЯ на tcp://*:5559 (порт зарезервирован за
    настройками камеры, PROMPT §4). Pi-сервис rpi_cam_ctl.py подключается
    сюда SUB-ом — топология «как у цветового SUB» (SUB connect, PUB bind).
    Подтверждений (ack) нет: канал односторонний, Pi-сервис логирует
    применение; окно настроек периодически повторяет полный набор
    (PUB теряет сообщения, пока SUB не подключился)."""

    PORT = 5559
    RESEND_INTERVAL = 5.0  # повтор полного набора, с (PUB late-joiner)

    def __init__(self):
        if zmq is None:
            raise RuntimeError("pyzmq не установлен — Pi-бэкенд недоступен")
        self.ctx = zmq.Context()
        self.pub = self.ctx.socket(zmq.PUB)
        self.pub.setsockopt(zmq.LINGER, 0)
        self.pub.bind(f"tcp://*:{self.PORT}")
        self.last_send_t = 0.0

    def send(self, controls, cmd="set"):
        msg = build_pi_message(controls, cmd)
        try:
            self.pub.send_string(msg, flags=zmq.NOBLOCK)
            self.last_send_t = time.monotonic()
            return True
        except zmq.Again:
            return False
        except zmq.ZMQError as e:
            print(f"[cam_settings] ошибка отправки на Pi: {e}")
            return False

    def maybe_resend(self, controls):
        """Периодический повтор всего набора — страховка от late join."""
        if time.monotonic() - self.last_send_t >= self.RESEND_INTERVAL:
            self.send(controls)

    def close(self):
        try:
            self.pub.close(0)
        except Exception:
            pass
        try:
            self.ctx.term()
        except Exception:
            pass


# ===================== ОКНО НАСТРОЕК (cv2-трекбары) =====================
class CameraSettingsWindow:
    """Отдельное cv2-окно с трекбарами и кнопками на полотне.

    Открывается/закрывается toggle(). Пока открыто, главный цикл зовёт
    pump() раз в кадр (внутри один cv2.waitKey(1) — прокачка событий окна).
    Изменение трекбара применяется сразу к активному бэкенду; сохранение в
    JSON — с дебаунсом 1 с (и принудительно при закрытии окна).
    Fisheye-трекбары — отдельная сущность, здесь и не упоминаются.
    """

    WIN = "camera settings (CAM SET)"
    SAVE_DEBOUNCE = 1.0   # с после последнего изменения до автозаписи
    SEND_DEBOUNCE = 0.3   # с между отправками на Pi при движении трекбара

    # Idea 9: полотно расширено вниз под секцию GATE ROLES
    # (заголовок + 3 строки по 24 px).
    CANVAS_W, CANVAS_H = 660, 448
    BTN_H = 30
    ROLE_BTN_H = 20       # высота кнопки роли (компактнее основных кнопок)
    ROLE_BTN_W = 84       # ширина кнопки роли (влезает "opponent")

    def __init__(self, cap_getter=None, picam=None, source_getter=None,
                 settings_path=None, gate_store_obj=None, roles_changed_cb=None,
                 gate_colors=None):
        self.cap_ctl = LocalCameraCtl(cap_getter) if cap_getter else None
        self.picam = picam                     # PiCamCtl | None
        self.source_getter = source_getter     # callable -> ("local",None)|("pi",ip)|None
        self.settings_path = settings_path or default_settings_path()
        # Idea 9: владелец saved_gates.json (GateStore из cv_kostyl.py —
        # один владелец файла на процесс) + колбэк «роли изменились».
        self.gate_store_obj = gate_store_obj
        self.roles_changed_cb = roles_changed_cb
        self.gate_colors = gate_colors         # RGB-кортежи, как TRACK_COLORS
        self._roles_local = {}                 # fallback, если store не передан

        self.open = False
        self.backend = "local"                 # 'local' | 'pi'
        self.backend_manual = False            # True после ручного переключения
        self.controls = dict(SAFE_DEFAULTS)
        self.status = {}                       # {имя: {requested, readback, state, note}}
        self.dirty = False                     # есть несохранённые изменения
        self._last_change_t = 0.0
        self._last_send_t = 0.0
        self._last_saved_str = ""
        self._last_pi_ip = None
        self._last_sent_str = ""
        self._btn_rects = {}                   # имя -> (x1, y1, x2, y2)
        self._click = None                     # имя кнопки, пойманной мышью
        self._btn_flash = {}                   # имя -> кадров до сброса подсветки

    # ---------- работа трекбаров ----------
    def _tb_name(self, name):
        ctrl = CONTROLS[name]
        if name == "brightness":
            return "brightness -64..64"
        if name == "power_line_frequency":
            return "power_line 0/1/2 (50Hz)"
        if name == "exposure_dynamic_framerate":
            return "dyn framerate 0/1"
        if name == "exposure":
            return "exposure (manual)"
        return f"{name} {ctrl['min']}..{ctrl['max']}"

    def _tb_pos(self, name):
        ctrl = CONTROLS[name]
        return int(self.controls[name]) - ctrl["min"]

    def _read_trackbars(self):
        """Прочитать трекбары; применить изменившиеся контролы."""
        for name, ctrl in CONTROLS.items():
            try:
                pos = cv2.getTrackbarPos(self._tb_name(name), self.WIN)
            except cv2.error:
                return  # окно закрылось посреди чтения
            value = clamp(name, pos + ctrl["min"])
            if value != self.controls[name]:
                self.controls[name] = value
                self._on_control_changed(name)

    def _on_control_changed(self, name=None):
        self.dirty = True
        now = time.monotonic()
        self._last_change_t = now
        targets = CONTROLS.keys() if name is None else (name,)
        if self.backend == "local":
            if self.cap_ctl is not None:
                res = self.cap_ctl.apply({n: self.controls[n] for n in targets})
                self.status.update(res)
            else:
                # Честный статус: камера не открыта (не молчание).
                for n in targets:
                    self.status[n] = {"requested": self.controls[n],
                                      "readback": None, "state": "nocam",
                                      "note": "локальная камера не открыта"}
        elif self.backend == "pi":
            if self.picam is not None:
                if now - self._last_send_t >= self.SEND_DEBOUNCE:
                    if self.picam.send(self.controls):
                        self._last_sent_str = time.strftime("%H:%M:%S")
                    self._last_send_t = now
                for n in targets:
                    self.status[n] = {"requested": self.controls[n],
                                      "readback": None, "state": "sent",
                                      "note": "отправлено на Pi (без ack)"}
            else:
                for n in targets:
                    self.status[n] = {"requested": self.controls[n],
                                      "readback": None, "state": "n/a",
                                      "note": "Pi-канал недоступен "
                                              "(нет pyzmq или порт 5559 занят)"}

    def _set_all_trackbars(self):
        for name, ctrl in CONTROLS.items():
            try:
                cv2.setTrackbarPos(self._tb_name(name), self.WIN,
                                   self._tb_pos(name))
            except cv2.error:
                pass

    # ---------- кнопки на полотне ----------
    def _on_mouse(self, event, x, y, flags, param):
        if event != cv2.EVENT_LBUTTONDOWN:
            return
        for name, (x1, y1, x2, y2) in self._btn_rects.items():
            if x1 <= x <= x2 and y1 <= y <= y2:
                self._click = name
                return

    def _handle_click(self):
        if self._click is None:
            return
        name, self._click = self._click, None
        if name == "defaults":
            self.controls = dict(SAFE_DEFAULTS)
            self._set_all_trackbars()
            self._on_control_changed(None)
            self._btn_flash["defaults"] = 20
        elif name == "save":
            self._save_now()
            self._btn_flash["save"] = 20
        elif name == "send":
            if self.picam is not None:
                if self.picam.send(self.controls):
                    self._last_sent_str = time.strftime("%H:%M:%S")
                    self._last_send_t = time.monotonic()
            self._btn_flash["send"] = 20
        elif name == "backend":
            self.backend = "pi" if self.backend == "local" else "local"
            self.backend_manual = True
            # при смене бэкенда синхронизируем полный набор
            self._on_control_changed(None)
        elif name.startswith("role") and "_" in name:
            # Idea 9: имена кнопок ролей — "role<gid>_<role>".
            gid_s, _, role = name[4:].partition("_")
            try:
                gid = int(gid_s)
            except ValueError:
                return
            self._set_gate_role(gid, role)
            self._btn_flash[name] = 20

    # ---------- роли ворот (Idea 9) ----------
    def _gate_role(self, gate_id):
        """Текущая роль слота: "ours" | "opponent" | "unset"."""
        if self.gate_store_obj is not None:
            try:
                return gate_store.normalize_role(
                    self.gate_store_obj.get_role(gate_id))
            except Exception as e:
                print(f"[CAM SET] не прочитал роль G{gate_id}: {e}")
                return gate_store.ROLE_UNSET
        return gate_store.normalize_role(self._roles_local.get(gate_id))

    def _set_gate_role(self, gate_id, role):
        """Клик по роли: записать в gate_store (атомарно) и уведомить UI.

        Роль живёт в saved_gates.json рядом с цветом слота; в
        camera_settings.json она НЕ пишется (разные контракт-файлы).
        """
        role = gate_store.normalize_role(role)
        if self.gate_store_obj is not None:
            try:
                self.gate_store_obj.set_role(gate_id, role)
            except Exception as e:
                print(f"[CAM SET] не записал роль G{gate_id}={role}: {e}")
        else:
            self._roles_local[gate_id] = role
        print(f"[CAM SET] G{gate_id} role = {role} (saved_gates.json)")
        if self.roles_changed_cb is not None:
            try:
                self.roles_changed_cb()
            except Exception as e:
                print(f"[CAM SET] roles_changed_cb упал: {e}")

    # ---------- открыть/закрыть ----------
    def toggle(self):
        if self.open:
            self.close()
        else:
            self.open_window()

    def open_window(self):
        if self.open:
            return
        # Приоритет инициализации: сохранённый файл > безопасные дефолты.
        # Текущие значения камеры НЕ читаем для инициализации: на проверенном
        # железе (ELP/MSMF, 2026-11-17) читка «залипшая» — cap.get() возвращает
        # замороженные значения, не отражающие реальные настройки, и показывать
        # их как «текущее состояние камеры» было бы ложью. Окно показывает
        # ЖЕЛАЕМОЕ состояние (файл/дефолты) и честный статус применения.
        saved = load_settings(self.settings_path)
        if saved:
            self.controls = dict(saved)
        else:
            self.controls = dict(SAFE_DEFAULTS)

        self.backend = self._auto_backend()
        self.backend_manual = False
        self.status = {}

        cv2.namedWindow(self.WIN, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(self.WIN, self.CANVAS_W, self.CANVAS_H + 8 * 40)
        for name, ctrl in CONTROLS.items():
            cv2.createTrackbar(self._tb_name(name), self.WIN,
                               self._tb_pos(name), ctrl["max"] - ctrl["min"],
                               lambda v: None)
        cv2.setMouseCallback(self.WIN, self._on_mouse)
        self.open = True
        # Применяем весь набор сразу (локальная камера увидит сохранённое).
        self._on_control_changed(None)
        print(f"[CAM SET] Окно открыто; backend={self.backend}, "
              f"файл={self.settings_path}")

    def close(self):
        if not self.open:
            return
        self.open = False
        self._save_now()
        try:
            cv2.destroyWindow(self.WIN)
        except cv2.error:
            pass
        print("[CAM SET] Окно закрыто, настройки сохранены")

    def _auto_backend(self):
        if self.backend_manual:
            return self.backend
        if self.source_getter is not None:
            try:
                src = self.source_getter()
            except Exception:
                src = None
            if isinstance(src, tuple) and src and src[0] == "pi":
                if src[1]:
                    self._last_pi_ip = src[1]
                return "pi"
        # По умолчанию — локальная камера (её чаще всего и настраивают).
        return "local" if self.cap_ctl is not None else "pi"

    def _save_now(self):
        try:
            save_settings(self.settings_path, self.controls)
            self.dirty = False
            self._last_saved_str = time.strftime("%H:%M:%S")
        except Exception as e:
            self._last_saved_str = f"ОШИБКА записи: {e}"
            print(f"[CAM SET] Не записал {self.settings_path}: {e}")

    # ---------- основной вызов из главного цикла ----------
    def is_open(self):
        """Открыто ли окно настроек (для координации waitKey с окном радара)."""
        return bool(getattr(self, "open", False))

    def pump(self):
        """Прокачка окна: трекбары, клики, автосохранение, отрисовка.
        Зовётся из главного цикла cv_kostyl.py каждый кадр; когда окно
        закрыто, стоит почти ничего (одна проверка)."""
        if not self.open:
            return

        # Окно могли закрыть крестиком.
        try:
            visible = cv2.getWindowProperty(self.WIN, cv2.WND_PROP_VISIBLE)
        except cv2.error:
            visible = 0.0
        if not visible or visible < 1.0:
            self.close()
            return

        # Бэкенд авто-следует за источником видео, пока пользователь не щёлкнул.
        if not self.backend_manual:
            self.backend = self._auto_backend()

        self._read_trackbars()
        self._handle_click()

        # Автосохранение с дебаунсом: не пишем диск на каждый шаг трекбара.
        if self.dirty and time.monotonic() - self._last_change_t >= self.SAVE_DEBOUNCE:
            self._save_now()

        # Pi: повтор полного набора, пока окно открыто (PUB late join).
        if self.backend == "pi" and self.picam is not None:
            self.picam.maybe_resend(self.controls)

        self._draw()
        cv2.imshow(self.WIN, self._canvas)
        cv2.waitKey(1)

    # ---------- отрисовка ----------
    def _draw(self):
        self._btn_rects = {}
        W, H = self.CANVAS_W, self.CANVAS_H
        self._canvas = np.zeros((H, W, 3), dtype=np.uint8)

        def put(text, x, y, color=(220, 220, 220), scale=0.42, thick=1):
            cv2.putText(self._canvas, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
                        scale, color, thick, cv2.LINE_AA)

        def button(name, label, x, y, w):
            flash = self._btn_flash.get(name, 0) > 0
            if flash:
                self._btn_flash[name] -= 1
            x2, y2 = x + w, y + self.BTN_H
            self._btn_rects[name] = (x, y, x2, y2)
            cv2.rectangle(self._canvas, (x, y), (x2, y2),
                          (255, 255, 255) if flash else (70, 70, 70), -1)
            (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX,
                                          0.45, 1)
            put(label, x + (w - tw) // 2, y + (self.BTN_H + th) // 2,
                (0, 0, 0) if flash else (255, 255, 255), 0.45, 1)

        put("Camera settings (Idea 1)", 8, 20, (255, 255, 255), 0.55, 1)
        pi_ip = self._last_pi_ip or (self._pi_ip_from_source() or "нет IP")
        put(f"backend: {self.backend}  |  AE: MANUAL (locked)", 260, 20,
            (180, 220, 180), 0.45, 1)

        y = 46
        state_colors = {
            "ok": (120, 220, 120), "mismatch": (80, 170, 255),
            "unconfirmed": (255, 190, 120), "n/a": (150, 150, 150),
            "nocam": (150, 150, 150), "fail": (80, 80, 255),
            "sent": (120, 200, 255),
        }
        notes = []  # пояснения для всех не-ok контролов («никогда не молча»)
        for name, ctrl in CONTROLS.items():
            st = self.status.get(name, {})
            state = st.get("state", "")
            color = state_colors.get(state, (160, 160, 160))
            rb = st.get("readback")
            rb_txt = "-" if rb is None else f"{rb:.0f}"
            line = (f"{name:<26s} {self.controls[name]:>5d}"
                    f"   got {rb_txt:>6s}  [{state or '-'}]")
            put(line, 12, y, color)
            note = st.get("note") or ""
            if state and state not in ("ok", "sent") and note:
                notes.append(f"{name}: {note}")
            y += 18

        # Пояснения (до 3 строк, обрезка по ширине полотна) — почему статус
        # не «ok»: MSMF-читка залипла, контрол не поддержан cv2 и т.п.
        for note in notes[:3]:
            put(note[:74], 12, y + 2, (140, 140, 140), 0.36, 1)
            y += 16

        if self.controls.get("brightness", 0) > 0:
            put("WARNING: brightness>0 вымывает цвета (HSV S 168->20) — "
                "лучше gain/gamma", 12, y + 2, (80, 170, 255), 0.42, 1)
            y += 20

        put(f"saved: {self._last_saved_str or '-'}  ->  {self.settings_path}",
            12, y + 4, (170, 170, 170))
        y += 22
        if self.backend == "pi":
            put(f"Pi: последний send {self._last_sent_str or '-'} "
                f"(target tcp://{pi_ip}:5559, без ack)",
                12, y + 4, (255, 190, 120))
        else:
            put(f"Pi недоступен в backend=local (кнопка backend переключает; "
                f"последний IP: {pi_ip})", 12, y + 4, (140, 140, 140))
        y += 24

        button("defaults", "SAFE DEFAULTS", 12, y, 130)
        button("save", "SAVE NOW", 152, y, 100)
        button("send", "SEND TO PI", 262, y, 110)
        button("backend", f"backend: {self.backend}", 382, y, 140)

        # ---------- Idea 9: GATE ROLES ----------
        # Роль каждого слота 1..3: ours / opponent / unset. Кнопки на
        # полотне (трекбар cv2 числовой — подписи «кто есть кто» на нём
        # не прочитать). Выбранная роль — залитая цветная кнопка.
        y += self.BTN_H + 8
        put("GATE ROLES (saved_gates.json)", 12, y, (255, 255, 255), 0.45, 1)
        if self.gate_store_obj is None:
            put("no gate store: session only", 250, y, (150, 150, 150),
                0.40, 1)
        y += 18

        role_sel_colors = {
            "ours": (110, 210, 110),      # BGR зелёный — «наши»
            "opponent": (100, 100, 235),  # BGR красный — «противник»
            "unset": (140, 140, 140),
        }
        for gid in (1, 2, 3):
            # Свотч цвета ворот (TRACK_COLORS — RGB, полотно cv2 — BGR).
            if self.gate_colors and gid <= len(self.gate_colors):
                r, g, b = self.gate_colors[gid - 1][:3]
                cv2.rectangle(self._canvas, (12, y + 3), (24, y + 15),
                              (int(b), int(g), int(r)), -1)
                cv2.rectangle(self._canvas, (12, y + 3), (24, y + 15),
                              (60, 60, 60), 1)
            put(f"G{gid}", 32, y + 14, (220, 220, 220), 0.45, 1)
            current = self._gate_role(gid)
            x = 62
            for role_name in gate_store.ROLES:
                btn_name = f"role{gid}_{role_name}"
                flash = self._btn_flash.get(btn_name, 0) > 0
                if flash:
                    self._btn_flash[btn_name] -= 1
                selected = current == role_name
                x2 = x + self.ROLE_BTN_W
                y2 = y + self.ROLE_BTN_H
                self._btn_rects[btn_name] = (x, y, x2, y2)
                if selected or flash:
                    cv2.rectangle(self._canvas, (x, y), (x2, y2),
                                  role_sel_colors[role_name], -1)
                    tcol = (0, 0, 0)
                else:
                    cv2.rectangle(self._canvas, (x, y), (x2, y2), (70, 70, 70), -1)
                    cv2.rectangle(self._canvas, (x, y), (x2, y2),
                                  (120, 120, 120), 1)
                    tcol = (255, 255, 255)
                (tw, th), _ = cv2.getTextSize(
                    role_name, cv2.FONT_HERSHEY_SIMPLEX, 0.42, 1)
                put(role_name, x + (self.ROLE_BTN_W - tw) // 2,
                    y + (self.ROLE_BTN_H + th) // 2, tcol, 0.42, 1)
                x = x2 + 6
            y += self.ROLE_BTN_H + 4

    def _pi_ip_from_source(self):
        if self.source_getter is None:
            return None
        try:
            src = self.source_getter()
        except Exception:
            return None
        if isinstance(src, tuple) and src and src[0] == "pi" and src[1]:
            self._last_pi_ip = src[1]
            return src[1]
        return None


# ===================== ПОДКЛЮЧЕНИЕ К ГЛАВНОМУ UI =====================
BTN_LABEL = "CAM SET"
BTN_X, BTN_Y, BTN_W, BTN_H = 460, 14, 105, 36  # правее btn_pause в верхнем ряду


def install(unikostyl_mod, cap_getter=None, source_getter=None,
            settings_path=None, gate_store_obj=None, roles_changed_cb=None,
            gate_colors=None):
    """Подключение к cv_kostyl.py: одна кнопка CAM SET + один вызов pump().

    - Добавляет кнопку в unikostyl.widgets/wnames (модуль при этом не правится);
      кнопка открывает/закрывает cv2-окно настроек.
    - Создаёт PiCamCtl (PUB bind :5559); если pyzmq нет или порт занят —
      Pi-бэкенд отключается с предупреждением, локальный продолжает работать.
    - Если camera_settings.json уже существует — применяет его к локальной
      камере прямо сейчас (восстановление настроек после перезапуска).
      Дефолты при отсутствии файла НЕ применяются (не трогаем текущее
      состояние камеры).
    - Idea 9: gate_store_obj (GateStore из cv_kostyl.py — единый владелец
      saved_gates.json) включает секцию GATE ROLES; roles_changed_cb зовётся
      после смены роли; gate_colors — RGB-кортежи TRACK_COLORS для свотчей
      (по умолчанию берутся из unikostyl_mod.TRACK_COLORS).
    Возвращает CameraSettingsWindow — caller зовёт .pump() каждый кадр
    и .close() при завершении.
    """
    from widgets import Button, Label  # локальный импорт: pygame есть в UI

    picam = None
    if zmq is not None:
        try:
            picam = PiCamCtl()
        except Exception as e:
            print(f"[CAM SET] Pi-бэкенд выключен: {e}")

    if gate_colors is None:
        gate_colors = getattr(unikostyl_mod, "TRACK_COLORS", None)

    win = CameraSettingsWindow(cap_getter=cap_getter, picam=picam,
                               source_getter=source_getter,
                               settings_path=settings_path,
                               gate_store_obj=gate_store_obj,
                               roles_changed_cb=roles_changed_cb,
                               gate_colors=gate_colors)

    button = Button(
        unikostyl_mod.screen,
        pygame.Rect(BTN_X, BTN_Y, BTN_W, BTN_H),
        label=Label(
            unikostyl_mod.screen,
            pygame.Rect(BTN_X, BTN_Y, BTN_W, BTN_H),
            BTN_LABEL, color=(255, 255, 255),
            font=pygame.font.Font(None, 26),
            stratch=True,
        ),
        func=win.toggle,
    )
    # Регистрируем кнопку в уже созданном UI: dict + список имён.
    unikostyl_mod.widgets["btn_cam_settings"] = button
    if "btn_cam_settings" not in unikostyl_mod.wnames:
        unikostyl_mod.wnames.append("btn_cam_settings")

    # Восстановление сохранённых настроек при старте (только если файл есть).
    saved = load_settings(win.settings_path)
    if saved and win.cap_ctl is not None:
        res = win.cap_ctl.apply(saved)
        bad = {n: s["state"] for n, s in res.items() if s["state"] not in ("ok",)}
        print(f"[CAM SET] camera_settings.json применён к локальной камере"
              f"{' (не применилось: ' + str(bad) + ')' if bad else ''}")

    return win
