#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gate_store.py — Idea 3: сохранение/восстановление цветов ворот
(contract-файл saved_gates.json, PROMPT_EN.md §4/§5 Idea 3).

Чистый stdlib (json/os/time): ни cv2, ни numpy, ни zmq, ни pygame —
модуль можно импортировать и на ноутбуке (cv_kostyl.py), и headless на Pi
(rpi_elp_cam.py), и использовать как эталон логики для C++-ядра (Idea 4).

Файл лежит РЯДОМ С МОДУЛЕМ: UniKostyl/saved_gates.json на ноутбуке,
/home/twox/saved_gates.json на Pi (gate_store.py деплоится рядом с
rpi_elp_cam.py). Схема:

    {"version": 1,
     "gates": {"1": {"rgb": [r, g, b],
                     "lab": [Lmin, Lmax, Amin, Amax, Bmin, Bmax] | null,
                     "enabled": true,
                     "ts": "2026-11-17T19:00:00"},
               "2": {...}, "3": {...}}}

Правила:
  * Захват SAVE Gn -> немедленная атомарная перезапись слота n (ноутбук);
    Pi обновляет свою копию, когда цвета приходят по 5557.
  * Запись атомарна: tmp-файл в ТОЙ ЖЕ директории + fsync + os.replace
    (атомарно и на Windows, и на POSIX). Читатель видит либо старый файл,
    либо новый — битый на пол-записи появиться не может (Pi может потерять
    питание посреди матча).
  * Битый/чужой файл при чтении -> пустой набор + WARN, никогда не
    исключение: рестарт Pi с повреждённым файлом не должен ронять камеру.
  * Неизвестные поля записи сохраняются при перезаписи (upsert): "role"
    (Идея 9) и поля Идеи 10 переживают SAVE без участия их агентов.
  * Роль ворот (Идея 9): "ours" (наши) / "opponent" (чужие) / "unset".
    В файле роль хранится ТОЛЬКО когда задана (ours/opponent); «unset» —
    это отсутствие ключа (единая семантика для файла, провода 5557 и
    статус-JSON 5555). Роль живёт вместе с цветом слота.
  * legacy thresholds.txt не читается и не пишется — это отдельный мир
    (OpenKostyl), не смешивать.
"""

import json
import os
import time

VERSION = 1
FILE_NAME = "saved_gates.json"
GATE_IDS = (1, 2, 3)

# Idea 9: роли ворот. Порядок фиксирован — индекс используется как позиция
# селектора в окне настроек (cam_settings.py).
ROLE_UNSET = "unset"
ROLES = ("ours", "opponent", ROLE_UNSET)

# «Не настроенный» LAB-диапазон; держать в синхроне с unikostyl.THRESHOLD_DEFAULT
# (слот, равный ему, не сохраняется и при загрузке не восстанавливается).
LAB_DEFAULT = (0, 100, -128, 127, -128, 127)

# Границы слайдеров ноутбука (unikostyl.widgets HorizSlider values):
# L: 0..100, A/B: -128..127. Значения вне диапазона обрезаются.
_LAB_LIMITS = ((0, 100), (-128, 127), (-128, 127), (-128, 127), (-128, 127))


def default_path():
    """saved_gates.json рядом с этим модулем (UniKostyl/ или /home/twox/)."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), FILE_NAME)


def _warn(msg):
    print(f"[GATE-STORE] [WARN] {msg}", flush=True)


def _clamp(v, lo, hi):
    return max(lo, min(hi, int(v)))


def valid_rgb(rgb):
    """RGB кортеж/список из 3 приводимых к int значений 0..255."""
    if not isinstance(rgb, (list, tuple)) or len(rgb) != 3:
        return False
    try:
        for c in rgb:
            c = int(c)
            if not (0 <= c <= 255):
                return False
    except (TypeError, ValueError):
        return False
    return True


def clamp_lab(lab):
    """6 значений LAB в пределах слайдеров; None/мусор -> None."""
    if not isinstance(lab, (list, tuple)) or len(lab) != 6:
        return None
    try:
        return [
            _clamp(lab[0], 0, 100),
            _clamp(lab[1], 0, 100),
            _clamp(lab[2], -128, 127),
            _clamp(lab[3], -128, 127),
            _clamp(lab[4], -128, 127),
            _clamp(lab[5], -128, 127),
        ]
    except (TypeError, ValueError):
        return None


def lab_configured(lab):
    """True, если LAB-диапазон отличается от стокового «весь кадр».

    Эквивалент unikostyl.slot_configured(): такой слот нельзя отслеживать
    (маска покроет весь кадр), поэтому как «настроенный» он не сохраняется.
    """
    lab = clamp_lab(lab)
    if lab is None:
        return False
    return any(v != d for v, d in zip(lab, LAB_DEFAULT))


def now_ts():
    return time.strftime("%Y-%m-%dT%H:%M:%S")


# ===================== РОЛИ ВОРОТ (Idea 9) =====================
def normalize_role(value):
    """Любое значение -> "ours" | "opponent" | "unset".

    None/мусор/"unset" -> "unset" (роль не задана). Никогда не бросает.
    """
    return value if value in ("ours", "opponent") else ROLE_UNSET


def stored_role(rec):
    """Роль записи слота (dict) или "unset", если записи/роли нет."""
    if not isinstance(rec, dict):
        return ROLE_UNSET
    return normalize_role(rec.get("role"))


def roles_map(gates):
    """{gate_id(int): "ours"|"opponent"} — только ЗАДАННЫЕ роли.

    Единый источник для провода 5557 (поле "role" добавляется только
    когда задано) и для бейджей панели статуса.
    """
    out = {}
    for gid in GATE_IDS:
        role = stored_role(gates.get(str(gid)))
        if role != ROLE_UNSET:
            out[gid] = role
    return out


# ===================== ЧТЕНИЕ =====================
def _normalize_record(rec):
    """Валидирует и нормализует запись слота. None -> запись не пригодна.

    Неизвестные поля (role — Идея 9 и т.д.) копируются как есть.
    """
    if not isinstance(rec, dict):
        return None
    out = dict(rec)
    out["rgb"] = (
        [_clamp(c, 0, 255) for c in rec.get("rgb")]
        if valid_rgb(rec.get("rgb"))
        else None
    )
    out["lab"] = clamp_lab(rec.get("lab"))
    out["enabled"] = bool(rec.get("enabled", True))
    if not isinstance(out.get("ts"), str):
        out["ts"] = ""
    if out["rgb"] is None and out["lab"] is None:
        return None  # ни цвета, ни порогов — запись бесполезна
    return out


def load_gates(path=None):
    """Читает saved_gates.json; {} если файла нет или он повреждён.

    Возвращает {"1": {...}, "2": {...}} — только валидные записи,
    ключи всегда строки "1".."3". Никогда не бросает исключений.
    """
    path = path or default_path()
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return {}
    except Exception as exc:
        _warn(f"{os.path.basename(path)} повреждён ({exc}) — начинаю с пустого набора")
        return {}
    if not isinstance(data, dict):
        _warn(f"{os.path.basename(path)}: корень не объект — игнорирую")
        return {}
    try:
        version = int(data.get("version", 0))
    except (TypeError, ValueError):
        version = 0
    if version != VERSION:
        _warn(f"{os.path.basename(path)}: версия {version} != {VERSION} — игнорирую")
        return {}
    gates = data.get("gates")
    if not isinstance(gates, dict):
        return {}
    out = {}
    for key, rec in gates.items():
        gid = str(key)
        if gid not in tuple(str(g) for g in GATE_IDS):
            _warn(f"{os.path.basename(path)}: ворота {gid!r} вне 1..3 — пропущены")
            continue
        clean = _normalize_record(rec)
        if clean is not None:
            out[gid] = clean
    return out


# ===================== ЗАПИСЬ =====================
def save_gates(path, gates):
    """Атомарная запись: tmp (та же директория) + fsync + os.replace.

    False при ошибке — старый файл остаётся нетронутым (tmp удаляется).
    Никогда не бросает исключений: вызывающий код в горячем цикле.
    """
    path = path or default_path()
    tmp_path = None
    try:
        payload = {
            "version": VERSION,
            "gates": {str(g): gates[str(g)] for g in GATE_IDS if str(g) in gates},
        }
        directory = os.path.dirname(path) or "."
        os.makedirs(directory, exist_ok=True)
        tmp_path = f"{path}.tmp.{os.getpid()}"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
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


# ===================== ИЗМЕНЕНИЕ (copy-on-write) =====================
def upsert(gates, gate_id, rgb, lab=None, enabled=True, ts=None, role=None):
    """Обновляет слот gate_id (1..3), возвращает НОВЫЙ словарь.

    copy-on-write: старые записи не мутируются — читающий поток
    (json.dump в другом потоке) всегда видит консистентный снимок.
    lab=None — НЕ трогать сохранённые LAB-пороги (у Pi их нет; ноутбук
    при SAVE передаёт актуальный диапазон). rgb=None — оставить прежний.
    role=None — НЕ трогать прежнюю роль; "ours"/"opponent" — задать;
    "unset" — снять (ключ "role" удаляется: отсутствие = unset).
    Идентификаторы вне 1..3 игнорируются (схема: только "1".."3").
    """
    gid = str(gate_id)
    if gid not in tuple(str(g) for g in GATE_IDS):
        _warn(f"upsert: ворота {gid!r} вне 1..3 — пропущены")
        return gates
    prev = gates.get(gid)
    rec = dict(prev) if isinstance(prev, dict) else {}
    if rgb is not None:
        if valid_rgb(rgb):
            rec["rgb"] = [_clamp(c, 0, 255) for c in rgb]
        else:
            _warn(f"upsert: ворота {gid}: некорректный rgb={rgb!r} — оставляю прежний")
    elif "rgb" not in rec:
        rec["rgb"] = None  # схема: ключи rgb/lab/enabled/ts всегда присутствуют
    if lab is not None:
        clamped = clamp_lab(lab)
        if clamped is not None:
            rec["lab"] = clamped
        else:
            _warn(f"upsert: ворота {gid}: некорректный lab={lab!r} — оставляю прежний")
    elif "lab" not in rec:
        rec["lab"] = None
    if role is not None:
        if role in ("ours", "opponent"):
            rec["role"] = role
        elif role == ROLE_UNSET:
            rec.pop("role", None)  # unset == отсутствие ключа
        else:
            _warn(f"upsert: ворота {gid}: неизвестная роль {role!r} — оставляю прежнюю")
    rec["enabled"] = bool(enabled)
    rec["ts"] = ts or now_ts()
    out = dict(gates)
    out[gid] = rec
    return out


def iter_enabled_rgb(gates):
    """[(gate_id, (r, g, b)), ...] по возрастанию id — включённые ворота
    с валидным rgb. Единый источник и для автоотправки ноутбука,
    и для восстановления на Pi."""
    out = []
    for gid in GATE_IDS:
        rec = gates.get(str(gid))
        if not rec or not rec.get("enabled"):
            continue
        rgb = rec.get("rgb")
        if valid_rgb(rgb):
            out.append((gid, (int(rgb[0]), int(rgb[1]), int(rgb[2]))))
    return out


def same_rgb(gates, gate_id, rgb, enabled=True, role=None):
    """True, если слот уже содержит ровно этот цвет, флаг enabled и роль.

    Pi получает цвета повторно (автоотправка ноутбука) — без этой проверки
    каждый дубль перезаписывал бы файл впустую. role=None — роль не
    сравнивается (старый ноутбук её не шлёт); "ours"/"opponent"/"unset" —
    сравнивается (повтор с той же ролью тоже дубль).
    """
    rec = gates.get(str(gate_id))
    if not rec or not valid_rgb(rgb):
        return False
    want = [_clamp(c, 0, 255) for c in rgb]
    if rec.get("rgb") != want or bool(rec.get("enabled")) != bool(enabled):
        return False
    if role is not None and stored_role(rec) != normalize_role(role):
        return False
    return True


# ===================== ОДИН ВЛАДЕЛЕЦ ФАЙЛА =====================
class GateStore:
    """Владелец одного saved_gates.json (ноутбук или Pi).

    min_interval=0.0 (ноутбук): set_rgb пишет немедленно — SAVE-нажатия
    редкие, требование Идеи 3 — «немедленная атомарная перезапись».
    min_interval>0 (Pi): не чаще одного раза за min_interval секунд,
    отложенное изменение дописывается flush_if_due() из основного цикла
    (одна проверка float — копейки) и flush_pending() при остановке.
    """

    def __init__(self, path=None, min_interval=0.0):
        self.path = path or default_path()
        self.min_interval = max(0.0, float(min_interval))
        self.gates = load_gates(self.path)
        self._dirty = False
        self._last_write = 0.0

    def restore_rgb(self):
        """[(gate_id, (r, g, b)), ...] — что поднять при старте."""
        return iter_enabled_rgb(self.gates)

    def get_lab(self, gate_id):
        """Сохранённый LAB-диапазон слота или None."""
        rec = self.gates.get(str(gate_id))
        return rec.get("lab") if rec else None

    def get_role(self, gate_id):
        """Роль слота: "ours" | "opponent" | "unset" (Idea 9)."""
        return stored_role(self.gates.get(str(gate_id)))

    def roles(self):
        """{gate_id(int): role} — заданные роли текущего набора (Idea 9)."""
        return roles_map(self.gates)

    def set_role(self, gate_id, role):
        """Задать/снять роль слота и записать файл (клик в CAM SET).

        "unset" снимает роль (ключ "role" удаляется из записи). Цвет, LAB и
        enabled слота не трогаются. False — нет изменений/ошибка записи.
        """
        gid = str(gate_id)
        if gid not in tuple(str(g) for g in GATE_IDS):
            return False
        role = normalize_role(role)
        if role == stored_role(self.gates.get(gid)):
            return False  # роль не изменилась — файл не перезаписываем
        prev = self.gates.get(gid)
        prev_enabled = bool(prev.get("enabled", True)) if isinstance(prev, dict) else True
        self.gates = upsert(
            self.gates, gate_id, rgb=None, lab=None,
            enabled=prev_enabled, role=role,
        )
        return self.flush()

    def set_rgb(self, gate_id, rgb, lab=None, enabled=True, role=None):
        """Обновить слот и записать файл. False — изменений/ошибка записи.

        role=None — роль не трогается (цвет мог прийти от старого ноутбука
        без ролей); "ours"/"opponent" — задать; "unset" — снять. Дубль
        (тот же цвет И та же роль) файл не перезаписывает.
        """
        gid = str(gate_id)
        if gid not in tuple(str(g) for g in GATE_IDS):
            return False
        if role is not None and role not in ROLES:
            role = None  # неизвестное значение с провода — прежнюю роль не трогаем
        if lab is None and same_rgb(self.gates, gate_id, rgb, enabled, role):
            return False  # дубль (автоотправка ноутбука) — файл не трогаем
        self.gates = upsert(self.gates, gate_id, rgb=rgb, lab=lab,
                            enabled=enabled, role=role)
        if self.min_interval <= 0.0:
            return self.flush()
        if time.monotonic() - self._last_write >= self.min_interval:
            return self.flush()
        self._dirty = True
        return False

    def flush_if_due(self):
        """Дописать отложенное, если подошло время. Вызывать каждый кадр:
        одна проверка двух float, запись происходит не чаще min_interval."""
        if self._dirty and time.monotonic() - self._last_write >= self.min_interval:
            return self.flush()
        return False

    def flush_pending(self):
        """Форс-дописать отложенное при остановке (не смотрит на лимит)."""
        if self._dirty:
            return self.flush()
        return False

    def flush(self):
        self._dirty = False
        self._last_write = time.monotonic()
        return save_gates(self.path, self.gates)
