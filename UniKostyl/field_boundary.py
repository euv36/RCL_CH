#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
field_boundary.py — Idea 11, КАМЕРНАЯ часть: советчик границы поля
(PROMPT_EN.md §5 Idea 11; площадка — WALLED Entry-вариант, физически
выйти за поле нельзя, поэтому детектор ВСПОМОГАТЕЛЬНЫЙ/низкий приоритет).

Что делает: из вида сверху (направленная камера вниз, зелёный ковёр =
пол, белая разметка = тоже пол, за полем — стол/чёрный борт/тёмное)
оценивает по секторам вокруг робота (8..12) долю «не-пола»: доля выше
порога -> «граница близко» в этом секторе. Дёшево: даунскейл + HSV +
пороги по секторам, никакой тяжёлой сегментации, никаких
findNonZero/boundingRect (запрещены в проекте) — только inRange и
countNonZero (простой счётчик).

БЕЗОПАСНОСТЬ (главное требование — «никогда не блокировать движение
ложноположительным срабатыванием»):
  * это СОВЕТЧИК: модуль не содержит ни serial, ни zmq — структурно не
    может отправить что-либо на Arduino. EDGE-сообщение не вводится без
    явного одобрения пользователя (правило §4 PROMPT_EN);
  * гистерезис: опасность подтверждается 2 (по умолчанию; 2–3 по PROMPT)
    подряд «опасными» кадрами и снимается только после release_frames
    подряд «чистых» — мигание не доходит до потребителя;
  * пороги калибруются на реальном поле через секцию "boundary" файла
    field_map.json (заполняется на настоящем ковре позже);
  * белая разметка считается ПОЛОМ (отдельный HSV-диапазон), иначе
    центральная линия давала бы ложную «границу».

FUSION (место для платы фототранзисторов): показания платы сегодня НЕ
доходят до Pi/ноутбука — обратного канала Arduino->Pi нет (его проектирует
Идея 12 / PROMPT_ROBOT; молча канал не изобретаем). Точка подключения —
BoundaryDetector.process(frame_rgb, board_flags=...) / fuse(): как только
появится телеметрия, состояние платы подставляется вторым аргументом и
ПРИОРИТЕТ у платы (danger = board OR camera; board выигрывает конфликты).
До тех пор камера работает автономным советчиком.

Статус (Идея 2): status_extra() готовит additive-ключ верхнего уровня
"edge_sectors" для статус-JSON — формат фиксирован (список bool по
секторам + "edge_any"); ноутбучный парсер (status_panel.
parse_status_payload) сохраняет неизвестные ключи в extra, схема не
ломается (проверено out/boundary_check.py). На Pi ключ добавляет
build_gate_status / pi_core, когда детектор заведётся на Pi — паритет
C++ остаётся за Идеей 4/12.

Ограничение: в RING-режиме (fisheye_mode == 2) детектор НЕ вызывается —
геометрия полосы другая (развёртка 360°, «сектора вокруг центра» не
имеют смысла); в режимах 0/1 углы секторов совпадают с конвенцией
трекинга/радара (0° = вправо, против часовой).

# UNVERIFIED ON HW: детектор проверен ТОЛЬКО на синтетических кадрах
# (out/boundary_check.py: зелёный пол + белая линия + тёмное «за полем»)
# и на записанном кадре реальной сцены (out/scene_red_folder.png — это
# НЕ реальное поле). На настоящем ковре с реальным освещением пороги
# НЕ калибровались — обязательный шаг перед матчем.
"""

import math
import time

import cv2
import numpy as np

# --- умолчания порогов (секция "boundary" field_map.json перекрывает) ---
DEFAULT_PARAMS = {
    "enabled": True,
    "n_sectors": 8,        # PROMPT: 8–12 секторов вокруг робота
    "inner_frac": 0.24,    # внутренний радиус сектора, доля min(w, h);
                           # по умолчанию за зоной корпуса (self-mask 0.22)
    "outer_frac": 0.46,    # внешний радиус: как далеко смотрим (вписанный
                           # круг кадра ~0.5*min — не вылезаем)
    "floor_h_lo": 35,      # зелёный ковёр: диапазон H (OpenCV 0..179)
    "floor_h_hi": 85,
    "floor_s_min": 60,     # насыщенность ковра не ниже (серое — не пол)
    "floor_v_min": 40,     # яркость ковра не ниже (тени калибровать в поле)
    "white_s_max": 70,     # белая разметка: S ниже и V выше
    "white_v_min": 185,
    "edge_share": 0.55,    # доля «не-пола» в секторе -> кадр «опасный»
    "confirm_frames": 2,   # подтверждение опасности: 2–3 кадра (PROMPT)
    "release_frames": 3,   # снятие опасности: подряд чистых кадров
    "work_size": 128,      # рабочая сторона даунскейла, px (min(w, h))
}

_ARC = None  # однослотовый кэш секторных масок: (key) -> prep dict


def _warn(msg):
    print(f"[FIELD-BOUNDARY] [WARN] {msg}", flush=True)


def _clamp(v, lo, hi, default=None):
    try:
        if isinstance(v, bool):
            raise TypeError
        f = float(v)
    except (TypeError, ValueError):
        f = default if default is not None else lo
    return max(lo, min(hi, f))


def resolve_params(raw=None):
    """Сливает секцию "boundary" field_map.json с умолчаниями.

    Неизвестные ключи игнорируются, мусор -> умолчание/кламп. Никогда не
    бросает: конфиг может править пользователь в JSON-редакторе.
    """
    p = dict(DEFAULT_PARAMS)
    if not isinstance(raw, dict):
        return p
    p["enabled"] = bool(raw.get("enabled", p["enabled"]))
    p["n_sectors"] = int(_clamp(raw.get("n_sectors"), 8, 12, p["n_sectors"]))
    inner = _clamp(raw.get("inner_frac"), 0.05, 0.45, p["inner_frac"])
    outer = _clamp(raw.get("outer_frac"), 0.10, 0.90, p["outer_frac"])
    if inner >= outer:
        # Вырожденное кольцо — раздвигаем вместо деления на пустоту.
        _warn(f"boundary: inner_frac {inner:.2f} >= outer_frac {outer:.2f} "
              f"— outer сдвинут до {inner + 0.05:.2f}")
        outer = min(0.90, inner + 0.05)
    p["inner_frac"], p["outer_frac"] = inner, outer
    p["floor_h_lo"] = int(_clamp(raw.get("floor_h_lo"), 0, 179, p["floor_h_lo"]))
    p["floor_h_hi"] = int(_clamp(raw.get("floor_h_hi"), 0, 179, p["floor_h_hi"]))
    p["floor_s_min"] = int(_clamp(raw.get("floor_s_min"), 0, 255, p["floor_s_min"]))
    p["floor_v_min"] = int(_clamp(raw.get("floor_v_min"), 0, 255, p["floor_v_min"]))
    p["white_s_max"] = int(_clamp(raw.get("white_s_max"), 0, 255, p["white_s_max"]))
    p["white_v_min"] = int(_clamp(raw.get("white_v_min"), 0, 255, p["white_v_min"]))
    p["edge_share"] = _clamp(raw.get("edge_share"), 0.05, 0.95, p["edge_share"])
    p["confirm_frames"] = int(_clamp(raw.get("confirm_frames"), 1, 10,
                                     p["confirm_frames"]))
    p["release_frames"] = int(_clamp(raw.get("release_frames"), 1, 10,
                                     p["release_frames"]))
    p["work_size"] = int(_clamp(raw.get("work_size"), 48, 320, p["work_size"]))
    return p


# ===================== ГЕОМЕТРИЯ СЕКТОРОВ =====================
def _sector_masks(w, h, n_sectors, inner_px, outer_px):
    """Маски кольцевых секторов для кадра (w x h).

    Сектор k покрывает углы [k*360/n, (k+1)*360/n), 0° = вправо, против
    часовой (ТА ЖЕ конвенция, что у трекинга/радара/serial — y кадра
    переворачивается при расчёте угла). Возвращает (маски uint8 0/255,
    знаменатели = число пикселей сектора). Маски кэшируются (один слот):
    пересобираются только при смене размера/параметров.
    """
    global _ARC
    key = (w, h, n_sectors, inner_px, outer_px)
    cached = _ARC.get("key") if isinstance(_ARC, dict) else None
    if cached == key:
        return _ARC["masks"], _ARC["dens"]
    ys, xs = np.mgrid[0:h, 0:w]
    dx = xs - (w // 2)
    dy = (h // 2) - ys  # y кадра растёт вниз — переворот для CCW-углов
    r = np.hypot(dx, dy)
    ang = np.degrees(np.arctan2(dy, dx)) % 360.0
    step = 360.0 / n_sectors
    idx = (ang / step).astype(np.int32) % n_sectors
    band = (r >= inner_px) & (r <= outer_px)
    masks, dens = [], []
    for k in range(n_sectors):
        m = ((idx == k) & band).astype(np.uint8) * 255
        masks.append(m)
        dens.append(int(np.count_nonzero(m)))
    _ARC = {"key": key, "masks": masks, "dens": dens}
    return masks, dens


def _floor_mask(hsv, p):
    """Маска «пола»: зелёный ковёр + белая разметка (hue-wrap учтён).

    cv2.max без out= в OpenCV 5 не работает — на 0/255-масках OR == max,
    поэтому объединение через bitwise_or(dst=...), без аллокаций.
    """
    lo = np.array([p["floor_h_lo"], p["floor_s_min"], p["floor_v_min"]], np.uint8)
    hi = np.array([p["floor_h_hi"], 255, 255], np.uint8)
    if p["floor_h_lo"] <= p["floor_h_hi"]:
        floor = cv2.inRange(hsv, lo, hi)
    else:
        # Диапазон через 0/179 (например, 170..20) — два inRange + OR.
        lo1 = np.array([p["floor_h_lo"], p["floor_s_min"], p["floor_v_min"]], np.uint8)
        hi1 = np.array([179, 255, 255], np.uint8)
        lo2 = np.array([0, p["floor_s_min"], p["floor_v_min"]], np.uint8)
        hi2 = np.array([p["floor_h_hi"], 255, 255], np.uint8)
        floor = cv2.inRange(hsv, lo1, hi1)
        cv2.bitwise_or(floor, cv2.inRange(hsv, lo2, hi2), dst=floor)
    # Белая разметка — ТОЖЕ пол (иначе центральная линия = ложная граница).
    wlo = np.array([0, 0, p["white_v_min"]], np.uint8)
    whi = np.array([179, p["white_s_max"], 255], np.uint8)
    cv2.bitwise_or(floor, cv2.inRange(hsv, wlo, whi), dst=floor)
    return floor


def detect_sectors(frame_rgb, params):
    """Один кадр -> (raw_flags, shares): сырые флаги секторов и доли
    «не-пола». Чистая функция состояния не имеет.

    frame_rgb — ЛЮБОЙ размер (RGB), даунскейл с СОХРАНЕНИЕМ пропорций
    (квадрат искажал бы углы секторов; портретный кадр ноутбука
    720x1280 должен давать правильные направления).
    """
    h, w = frame_rgb.shape[:2]
    if w < 4 or h < 4:
        n = params["n_sectors"]
        return [False] * n, [0.0] * n
    work = params["work_size"]
    scale = work / float(min(w, h))
    w2 = max(2, int(round(w * scale)))
    h2 = max(2, int(round(h * scale)))
    # INTER_LINEAR, НЕ INTER_AREA: замер на этом PC (OpenCV 5.1, вход
    # 720x1280 -> 128x228) — AREA 6.5 мс, LINEAR 0.26 мс, NEAREST 0.07 мс.
    # Советчику достаточно LINEAR: доля «не-пола» усредняется по тысячам
    # пикселей сектора, единичные выбросы на роль флага не влияют.
    small = cv2.resize(frame_rgb, (w2, h2), interpolation=cv2.INTER_LINEAR)
    hsv = cv2.cvtColor(small, cv2.COLOR_RGB2HSV)
    floor = _floor_mask(hsv, params)
    non = cv2.bitwise_not(floor)  # «не-пол»: за полем/борт/ препятствие
    min_dim = min(w2, h2)
    inner_px = int(round(params["inner_frac"] * min_dim))
    outer_px = int(round(params["outer_frac"] * min_dim))
    masks, dens = _sector_masks(w2, h2, params["n_sectors"], inner_px, outer_px)
    shares = []
    raw = []
    tmp = np.empty_like(non)  # переиспользуемый буфер (без роста аллокаций)
    for mask, den in zip(masks, dens):
        if den <= 0:
            shares.append(0.0)
            raw.append(False)
            continue
        cv2.bitwise_and(non, mask, dst=tmp)
        shares.append(cv2.countNonZero(tmp) / float(den))
        raw.append(shares[-1] >= params["edge_share"])
    return raw, shares


# ===================== ГИСТЕРЕЗИС =====================
class EdgeHysteresis:
    """Опасность сектора подтверждается confirm_frames подряд «опасных»
    кадров (2–3 по PROMPT) и снимается после release_frames подряд
    «чистых» — мигание одной рамки не доходит до потребителя."""

    def __init__(self, n_sectors, confirm_frames, release_frames):
        self.n = max(1, int(n_sectors))
        self.confirm = max(1, int(confirm_frames))
        self.release = max(1, int(release_frames))
        self._cand = [0] * self.n   # подряд «опасных» кадров
        self._clear = [0] * self.n  # подряд «чистых» кадров
        self.flags = [False] * self.n  # подтверждённая опасность

    def update(self, raw_flags):
        raw = list(raw_flags) + [False] * (self.n - len(raw_flags or ()))
        for k in range(self.n):
            if raw[k]:
                self._cand[k] += 1
                self._clear[k] = 0
                if self._cand[k] >= self.confirm:
                    self.flags[k] = True
            else:
                self._clear[k] += 1
                self._cand[k] = 0
                if self._clear[k] >= self.release:
                    self.flags[k] = False
        return list(self.flags)

    def reset(self):
        self._cand = [0] * self.n
        self._clear = [0] * self.n
        self.flags = [False] * self.n


# ===================== FUSION (заглушка Идеи 12) =====================
def fuse(camera_state, board_flags=None):
    """danger = плата OR камера; ПРИОРИТЕТ У ПЛАТЫ (PROMPT Idea 11).

    camera_state: dict с ключами "flags" (подтверждённые камерой) и "n",
    либо None. board_flags: None = обратного канала НЕТ (сегодня);
    список/кортеж bool по секторам = состояния платы фототранзисторов
    (Идея 12 подставит сюда телеметрию; молча канал не изобретается).
    Возвращает подтверждённый список опасных секторов — ЕДИНСТВЕННОЕ,
    что видит потребитель (радар/лог/статус).
    """
    cam = []
    n = 0
    if isinstance(camera_state, dict):
        cam = camera_state.get("flags") or []
        try:
            n = int(camera_state.get("n", len(cam)))
        except (TypeError, ValueError):
            n = len(cam)
    out = []
    for k in range(max(n, len(board_flags or ()))):
        c = bool(cam[k]) if k < len(cam) else False
        b = bool(board_flags[k]) if board_flags is not None and k < len(board_flags) else False
        out.append(b or c)
    return out


# ===================== ДЕТЕКТОР (состояние) =====================
class BoundaryDetector:
    """Обёртка для живого конвейера: параметры + гистерезис в одном месте.

    unikostyl держит ОДИН экземпляр и зовёт process() на каждом РЕАЛЬНОМ
    кадре (не на паузе, не в RING). board_flags — точка подключения платы
    (Идея 12): None сегодня, список bool по секторам после внедрения
    обратного канала.
    """

    def __init__(self, params=None):
        self.params = resolve_params(params)
        self.hyst = EdgeHysteresis(
            self.params["n_sectors"],
            self.params["confirm_frames"],
            self.params["release_frames"],
        )

    def process(self, frame_rgb, board_flags=None):
        """Кадр -> состояние или None (нет кадра / детектор выключен)."""
        if frame_rgb is None or not self.params["enabled"]:
            return None
        raw, shares = detect_sectors(frame_rgb, self.params)
        camera_flags = self.hyst.update(raw)
        fused = fuse({"flags": camera_flags, "n": len(camera_flags)},
                     board_flags)
        return {
            "n": len(fused),
            "raw": raw,
            "camera_flags": camera_flags,
            "flags": fused,           # то, что видит потребитель (плата OR камера)
            "shares": [round(s, 4) for s in shares],
            "any": any(fused),
            "board_used": board_flags is not None,  # True, когда Идея 12 подключена
            "ts": time.monotonic(),
        }


# ===================== СТАТУС (additive-ключ Идеи 2) =====================
def status_extra(state):
    """Additive-ключи верхнего уровня для статус-JSON Идеи 2.

    {"edge_sectors": [bool по секторам], "edge_any": bool}. Формат
    фиксирован; ноутбучный parse_status_payload сохраняет неизвестные
    ключи в extra (проверено), Pi-сторона добавляет ключ, когда детектор
    там заведётся. Схема gates/fps не меняется — never break the schema.
    """
    flags = list(state.get("flags", [])) if isinstance(state, dict) else []
    return {"edge_sectors": flags, "edge_any": any(flags)}
