#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
field_map.py — Idea 10 (привязка к полю) + секция порогов Идеи 11
(contract-файл field_map.json, PROMPT_EN.md §4/§5 Ideas 10–11).

Чистый stdlib (json/os/time/math): ни cv2, ни numpy, ни pygame, ни zmq —
модуль импортируется на ноутбуке (cv_kostyl.py/unikostyl.py), headless на
Pi (rpi_elp_cam.py / будущий pi_core / robot_core Идеи 12) и тестируется
без камеры. Файл лежит РЯДОМ С МОДУЛЕМ: UniKostyl/field_map.json на
ноутбуке, /home/twox/field_map.json на Pi.

Схема (PROMPT_EN.md §5 Idea 10; ключи верхнего уровня фикс, «boundary» —
секция порогов Идеи 11, неизвестные ключи сохраняются при перезаписи):

    {"version": 1,
     "bound_at": "2026-11-17T21:40:00" | null,     -- момент привязки
     "robot_pos": "center"|"own_goal"|"manual"|null,
     "heading_offset_deg": 0.0,                    -- курс камеры к полю
     "gates": {"1": {"angle_deg": 123.0,         -- полярные координаты
                     "dist_cm": 148.0 | null,    -- ворот относительно РОБОТА
                     "estimated": false},        -- в момент привязки
               "2": {...}},
     "boundary": {...}}                            -- пороги Идеи 11 (см.
                                                   -- field_boundary.py)

Семантика:
  * Привязка — ПРИБЛИЗИТЕЛЬНЫЙ снимок при первичной настройке (НЕ SLAM):
    кнопка BIND FIELD в главном UI записывает угол/дистанцию каждого
    ИЗМЕРЕННОГО (found) ворот относительно робота в этот момент.
  * "estimated" в записи ворот: false = живое измерение в момент привязки;
    true = значение удержано гистерезисом Идеи 5 (маска пуста, ворота
    только что потеряны) — честная пометка «не свежее измерение».
  * Пока ворота вне вида, estimated_directions() отдаёт «last known»
    направления (радар рисует их приглушённо с пометкой «est»).
  * УЧЁТ ПОВОРОТА РОБОТА НЕ РЕАЛИЗОВАН и не будет изобретён молча: для него
    нужен обратный канал Arduino->Pi (курс/одометрия), которого сегодня НЕТ
    (его проектирует Идея 12 / PROMPT_ROBOT). Заглушка — apply_heading():
    чистая функция, никем не вызывается, знак поворота сверить при
    внедрении телеметрии. # UNVERIFIED ON HW: поворот не проверялся.
  * Запись атомарна (tmp в той же директории + fsync + os.replace) — тот же
    шаблон, что gate_store.save_gates: Pi может потерять питание посреди
    матча. Битый файл при чтении -> None + WARN, никогда не исключение.
  * Секция "boundary" (Идея 11) и неизвестные ключи ПЕРЕЖИВАЮТ перезапись
    привязки (copy-on-write, как gate_store.upsert).
  * Файл без привязки (gates пуст, bound_at null) — валиден: в нём живут
    калибровочные пороги границы поля до первой привязки.
"""

import json
import math
import os
import time

VERSION = 1
FILE_NAME = "field_map.json"
GATE_IDS = (1, 2, 3)

# Пресеты позиции робота при привязке (кнопка POS циклит по этому порядку).
ROBOT_POS_PRESETS = ("center", "own_goal", "manual")

# Дистанция ворот: рабочий диапазон Идеи 6 (0..500 см, поле максимум
# 225 см по диагонали регламента с запасом).
DIST_MAX_CM = 500.0


def default_path():
    """field_map.json рядом с этим модулем (UniKostyl/ или /home/twox/)."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), FILE_NAME)


def _warn(msg):
    print(f"[FIELD-MAP] [WARN] {msg}", flush=True)


def now_ts():
    return time.strftime("%Y-%m-%dT%H:%M:%S")


def normalize_robot_pos(value):
    """Любое значение -> пресет или None (null в файле = привязки нет)."""
    return value if value in ROBOT_POS_PRESETS else None


def _clamp_angle_deg(value):
    """Угол ворот 0..360 (нормализация % 360) или None для мусора."""
    try:
        if isinstance(value, bool):
            return None
        f = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(f):
        return None
    return f % 360.0


def _clamp_dist_cm(value):
    """Дистанция 0..DIST_MAX_CM или None (null = дистанция не измерена)."""
    try:
        if isinstance(value, bool):
            return None
        f = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(f) or f <= 0.0:
        return None
    return min(f, DIST_MAX_CM)


def normalize_gate_entry(rec):
    """Валидация записи ворот привязки. None -> запись отбрасывается.

    Возвращает {"angle_deg": float, "dist_cm": float|null,
    "estimated": bool} или None (нет угла — запись бесполезна: направление
    и есть содержимое привязки, дистанция опциональна).
    """
    if not isinstance(rec, dict):
        return None
    angle = _clamp_angle_deg(rec.get("angle_deg"))
    if angle is None:
        return None
    return {
        "angle_deg": angle,
        "dist_cm": _clamp_dist_cm(rec.get("dist_cm")),
        "estimated": bool(rec.get("estimated", False)),
    }


# ===================== СНИМКИ МОМЕНТА ПРИВЯЗКИ =====================
# Чистые функции: принимают уже готовые rec-словари (форматы unikostyl
# compute_track / status_panel.parse_status_payload), ничего не импортируют.
def snapshot_from_results(results):
    """Снимок из локального трекинга (unikostyl.track_results).

    rec: {slot, enabled, found, angle, dist_cm, held, ...}. Берём только
    НАСТРОЕННЫЕ (enabled) ворота с реальным направлением; held-значение
    (гистерезис Идея 5) помечается estimated=True — это не свежее
    измерение. Возвращает {gid(int): {"angle_deg", "dist_cm", "estimated"}}.
    """
    out = {}
    if not isinstance(results, (list, tuple)):
        return out
    for rec in results:
        if not isinstance(rec, dict):
            continue
        try:
            gid = int(rec.get("slot", -1)) + 1
        except (TypeError, ValueError):
            continue
        if gid not in GATE_IDS:
            continue
        if not rec.get("enabled", False):
            continue  # слот не настроен — привязывать нечего
        if not rec.get("found", False):
            continue  # нет измерения в момент привязки — честно пропустить
        angle = _clamp_angle_deg(rec.get("angle"))
        if angle is None:
            continue
        out[gid] = {
            "angle_deg": angle,
            "dist_cm": _clamp_dist_cm(rec.get("dist_cm")),
            "estimated": bool(rec.get("held", False)),
        }
    return out


def snapshot_from_pi_status(pi_status):
    """Снимок из разобранного статуса Pi (status_panel.parse_status_payload).

    {"gates": {"1": {"enabled": true, "found": true, "angle": 123,
                     "dist_cm": 148.0, "dist_reliable": true, "held": ...}}}.
    dist_reliable=False (заслонённые ворота, Идея 6) -> дистанция не
    записывается (null): недостоверное число в привязке хуже его absence.
    """
    out = {}
    if not isinstance(pi_status, dict):
        return out
    gates = pi_status.get("gates")
    if not isinstance(gates, dict):
        return out
    for key, g in gates.items():
        try:
            gid = int(key)
        except (TypeError, ValueError):
            continue
        if gid not in GATE_IDS or not isinstance(g, dict):
            continue
        if not g.get("enabled", False) or not g.get("found", False):
            continue
        angle = _clamp_angle_deg(g.get("angle"))
        if angle is None:
            continue
        dist = None
        if g.get("dist_reliable", True):
            dist = _clamp_dist_cm(g.get("dist_cm"))
        out[gid] = {
            "angle_deg": angle,
            "dist_cm": dist,
            "estimated": bool(g.get("held", False)),
        }
    return out


def build_map_data(snapshot, robot_pos, heading_offset_deg=0.0, prev=None):
    """Полный dict для field_map.json из снимка + пресета + курса.

    prev — предыдущий валидированный dict (или None): секция "boundary"
    (Идея 11) и НЕИЗВЕСТНЫЕ ключи верхнего уровня переносятся как есть,
    чтобы калибровка границы не терялась при перепривязке.
    """
    pos = normalize_robot_pos(robot_pos)
    if pos is None:
        _warn(f"build_map_data: пресет {robot_pos!r} не из "
              f"{ROBOT_POS_PRESETS} — записываю null")
    try:
        heading = float(heading_offset_deg)
        if not math.isfinite(heading):
            heading = 0.0
    except (TypeError, ValueError):
        _warn(f"build_map_data: heading {heading_offset_deg!r} не число — 0.0")
        heading = 0.0
    gates = {}
    for gid in GATE_IDS:
        entry = normalize_gate_entry(snapshot.get(gid))
        if entry is not None:
            gates[str(gid)] = entry
    data = {
        "version": VERSION,
        "bound_at": now_ts() if gates else None,
        "robot_pos": pos if gates else None,
        "heading_offset_deg": heading,
        "gates": gates,
    }
    if isinstance(prev, dict):
        # Чужие/будущие ключи + секция boundary Идеи 11 сохраняются.
        for key, val in prev.items():
            if key not in data:
                data[key] = val
        boundary = prev.get("boundary")
        if boundary is not None:
            data["boundary"] = boundary
    return data


def binding_report(data):
    """Короткая человекочитаемая сводка привязки (консоль при BIND)."""
    if not isinstance(data, dict):
        return "привязки нет"
    gates = data.get("gates") or {}
    parts = []
    for gid in GATE_IDS:
        e = gates.get(str(gid))
        if not e:
            continue
        txt = f"G{gid}={e['angle_deg']:.0f}°"
        if e.get("dist_cm") is not None:
            txt += f"/{e['dist_cm']:.0f}см"
        if e.get("estimated"):
            txt += "(est)"
        parts.append(txt)
    return (
        f"pos={data.get('robot_pos')}, heading={data.get('heading_offset_deg', 0.0):.0f}°"
        + (", " + " ".join(parts) if parts else ", ворота не измерены")
    )


# ===================== ЧТЕНИЕ =====================
def load_map(path=None):
    """Читает field_map.json; None если файла нет или он повреждён.

    Возвращает валидированный dict (неизвестные ключи сохранены, записи
    ворот нормализованы) или None + WARN. Никогда не бросает исключений:
    рестарт с битым файлом не роняет UI/камеру.
    """
    path = path or default_path()
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except Exception as exc:
        _warn(f"{os.path.basename(path)} повреждён ({exc}) — привязка игнорируется")
        return None
    if not isinstance(data, dict):
        _warn(f"{os.path.basename(path)}: корень не объект — игнорирую")
        return None
    try:
        version = int(data.get("version", 0))
    except (TypeError, ValueError):
        version = 0
    if version != VERSION:
        _warn(f"{os.path.basename(path)}: версия {version} != {VERSION} — игнорирую")
        return None

    out = dict(data)  # неизвестные ключи сохраняются
    pos = normalize_robot_pos(data.get("robot_pos"))
    if data.get("robot_pos") is not None and pos is None:
        _warn(f"{os.path.basename(path)}: robot_pos {data.get('robot_pos')!r} "
              f"не из {ROBOT_POS_PRESETS} — читаю как null")
    out["robot_pos"] = pos
    try:
        heading = float(data.get("heading_offset_deg", 0.0))
        if not math.isfinite(heading):
            heading = 0.0
    except (TypeError, ValueError):
        _warn(f"{os.path.basename(path)}: heading_offset_deg не число — 0.0")
        heading = 0.0
    out["heading_offset_deg"] = heading
    if not isinstance(out.get("bound_at"), (str, type(None))):
        out["bound_at"] = None

    gates = {}
    raw_gates = data.get("gates")
    if raw_gates is None:
        raw_gates = {}
    elif not isinstance(raw_gates, dict):
        _warn(f"{os.path.basename(path)}: gates не объект — привязка пуста")
        raw_gates = {}
    for key, rec in raw_gates.items():
        gid = str(key)
        if gid not in tuple(str(g) for g in GATE_IDS):
            _warn(f"{os.path.basename(path)}: ворота {gid!r} вне 1..3 — пропущены")
            continue
        clean = normalize_gate_entry(rec)
        if clean is None:
            _warn(f"{os.path.basename(path)}: запись ворот {gid} невалидна — пропущена")
            continue
        gates[gid] = clean
    out["gates"] = gates
    return out


# ===================== ЗАПИСЬ =====================
def save_map(path, data):
    """Атомарная запись: tmp (та же директория) + fsync + os.replace.

    False при ошибке — старый файл остаётся нетронутым (tmp удаляется).
    Никогда не бросает исключений: вызывается из UI-цикла.
    """
    path = path or default_path()
    tmp_path = None
    try:
        directory = os.path.dirname(path) or "."
        os.makedirs(directory, exist_ok=True)
        tmp_path = f"{path}.tmp.{os.getpid()}"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, path)  # атомарно на Windows и POSIX
    except Exception as exc:
        _warn(f"не удалось записать {os.path.basename(path)}: {exc}")
        try:
            if tmp_path and os.path.exists(tmp_path):
                os.remove(tmp_path)
        except Exception:
            pass
        return False
    _cleanup_stale_tmp(path)
    return True


def _cleanup_stale_tmp(path):
    """Подчищает tmp-файлы погибших процессов (имя включает pid автора)."""
    try:
        directory = os.path.dirname(path) or "."
        base = os.path.basename(path)
        for name in os.listdir(directory):
            if name.startswith(base + ".tmp.") and name != f"{base}.tmp.{os.getpid()}":
                try:
                    os.remove(os.path.join(directory, name))
                except OSError:
                    pass
    except OSError:
        pass


# ===================== «LAST KNOWN» НАПРАВЛЕНИЯ =====================
def estimated_directions(data, heading_delta_deg=0.0):
    """{gid(int): {"angle_deg", "dist_cm", "estimated": True}} — все записи
    привязки. Потребитель (радар) сам решает, когда их показывать (ворота
    вне вида); здесь отдаётся весь набор.

    heading_delta_deg — РЕЗЕРВ Идеи 12: накопленный поворот робота с
    момента привязки (обратный канал Arduino->Pi). СЕГОДНЯ всегда 0:
    канала нет, снимок статический, это документированное ограничение
    (PROMPT_EN §5 Idea 10 «honest degradation»). Знак вычитания сверить
    при внедрении телеметрии курса. # UNVERIFIED ON HW: поворот не
    проверялся (нет канала).
    """
    out = {}
    if not isinstance(data, dict):
        return out
    gates = data.get("gates")
    if not isinstance(gates, dict):
        return out
    delta = 0.0
    if heading_delta_deg:
        try:
            delta = float(heading_delta_deg)
        except (TypeError, ValueError):
            delta = 0.0
    for key, rec in gates.items():
        entry = normalize_gate_entry(rec)
        if entry is None:
            continue
        try:
            gid = int(key)
        except (TypeError, ValueError):
            continue
        if gid not in GATE_IDS:
            continue
        entry["estimated"] = True  # «last known» — всегда est для потребителя
        if delta:
            entry["angle_deg"] = (entry["angle_deg"] - delta) % 360.0
        out[gid] = entry
    return out


def apply_heading(data, heading_delta_deg):
    """Заглушка Идеи 12 (обратный канал Arduino->Pi: курс/одометрия).

    НИКЕМ НЕ ВЫЗЫВАЕТСЯ — канал не существует, изобретать его молча
    запрещено (PROMPT_EN §5 Idea 10 / §4). Когда Идея 12 добавит
    телеметрию, накопленный с момента привязки поворот подставляется
    сюда (или прямо в estimated_directions(..., heading_delta_deg=...))
    и результат скармливается радару тем же set_estimated(). Возвращает
    повернутые направления, исходные данные не мутирует.
    """
    return estimated_directions(data, heading_delta_deg=heading_delta_deg)


# ===================== СЕКЦИЯ ПОРОГОВ ИДЕИ 11 =====================
def load_boundary_section(path=None):
    """Сырая секция "boundary" из field_map.json ({} если файла/секции нет).

    Валидация значений — на стороне field_boundary.resolve_params() (там
    живут умолчания и клампы; этот модуль остаётся без cv2/numpy).
    Битый файл -> {} + WARN (уже напечатан load_map), секция = умолчания.
    """
    path = path or default_path()
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return {}
    except Exception:
        return {}  # WARN уже дал load_map при той же ошибке
    if not isinstance(data, dict):
        return {}
    section = data.get("boundary")
    return dict(section) if isinstance(section, dict) else {}
