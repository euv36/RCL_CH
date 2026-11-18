#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
distance.py — Idea 6: дистанция до ворот по их размеру
(contract-файл distance_calib.json, PROMPT_EN.md §5 Idea 6).

Модель — эмпирическая кривая из референса:

    d = a / (b - r) - c

где r — радиус слитого объекта ворот в px (Idea 5: описанная окружность
объединённого бокса кластера, gate_cluster.build_gate_object["radius"]),
d — расстояние в см. Константы a, b, c подбираются по калибровочным
точкам (робот на известной дистанции -> измеренный r); точки хранятся
в distance_calib.json рядом с модулем (UniKostyl/ на ноутбуке,
/home/twox/ на Pi — модуль деплоится рядом с rpi_elp_cam.py, как
gate_store.py / gate_cluster.py / self_mask.py).

Фит без scipy: линеаризация
    (d + c)(b - r) = a  =>  b*d + (c*b - a) - c*r = r*d
— линейная система [d, 1, -r] · (b, q, c) = r*d (q = c*b - a), решается
наименьшими квадратами (numpy.linalg.lstsq), затем полируется
несколько итераций Гаусса-Ньютона по исходной невязке. Для точных
(синтетических) данных линеаризация восстанавливает a,b,c точно.

Гарантии «без багов» (PROMPT §5 Idea 6):
  * деление: |b - r| > EPS_DIV, плюс r обязан лежать в интервале
    калибровки +-25% размаха (экстраполяция за асимптоту = мусор);
  * d зажимается в рабочий диапазон 0..500 см; выход за диапазон =
    недостоверное значение (dist_reliable: false);
  * модель проверяется при фитте И при загрузке: конечность, строгое
    УБЫВАНИЕ d по r (больше ворота = ближе; наклон = знак a, поэтому
    a < 0 обязателен), асимптота b вне вилки калибровочных r, нет NaN.
    Битая/неправдоподобная модель -> None -> аналитический fallback
    Z = W_широты * (ширина_кадра / pi) / w_px (форма формулы из
    unikostyl.py: FOCAL_DIVISOR = pi; ширина ворот — параметр
    регламента 45-60 см, по умолчанию 45);
  * перекрытие ворот противником: видимый размер занижается ->
    dist_reliable: false и УДЕРЖАНИЕ последнего достоверного значения
    T секунд (по умолчанию 2, настраивается) вместо скачка. Эвристики:
    кадр «held» от гистерезиса Idea 5 (маска пуста — размер не
    подтверждён), внезапное падение радиуса при неподвижном центроиде
    (ворота на месте, но внезапно «уменьшились» — кто-то заслонил),
    низкая заполненность бокса area/(w*h) (куски ворот через тело
    противника). Значение держится даже когда flags unreliable —
    потребитель (панель/радар) сам решает, показывать ли.

Запись файла атомарна: tmp в ТОЙ ЖЕ директории + fsync + os.replace
(Pi может потерять питание посреди матча), чтение никогда не бросает.

Модуль чистый: numpy + stdlib, ни cv2, ни zmq, ни pygame — импортируется
и на ноутбуке, и headless на Pi; C++-зеркало — pi_core/src/distance.{hpp,cpp}.
"""

import json
import math
import os
import time

import numpy as np

# -------- контракт (schema в COORDINATION.md, Idea 6) --------
VERSION = 1
FILE_NAME = "distance_calib.json"
MODEL_NAME = "a/(b-r)-c"

# -------- защита от мусора --------
D_MIN_CM = 0.0            # рабочий диапазон дистанции, см
D_MAX_CM = 500.0
EPS_DIV = 1e-6            # деление: |b - r| обязано превышать это
EXTRAP_FRAC = 0.25        # экстраполяция r за вилку калибровки, доля размаха
EXTRAP_MIN_PX = 10.0      # ...но не меньше, px
MIN_POINTS = 3            # минимум точек для фита (рекомендовано 4-6)

# -------- эвристики перекрытия / удержание (умолчания) --------
DEFAULT_HOLD_S = 2.0      # сколько секунд держать последнее достоверное d
DEFAULT_DROP_FRAC = 0.6   # r < 0.6*r_эталона при неподвижном центроиде = заслон
DEFAULT_FILL_MIN = 0.25   # area/(w*h) ниже — «дырки» в объекте (0 = выкл)
DEFAULT_MOVE_FRAC = 0.08  # сдвиг центроида больше — «ворота уехали», не заслон
DEFAULT_GATE_WIDTH_CM = 45.0   # ширина ворот по регламенту 45-60 см


def default_path():
    """distance_calib.json рядом с этим модулем (UniKostyl/ или /home/twox/)."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), FILE_NAME)


def _warn(msg):
    print(f"[DISTANCE] [WARN] {msg}", flush=True)


def analytic_distance_cm(w_px, frame_w, gate_width_cm=DEFAULT_GATE_WIDTH_CM):
    """Fallback без калибровки: Z = W * (ширина_кадра / pi) / w_px.

    Тот же вид, что в unikostyl.py (FOCAL_DIVISOR = pi, равноугольная
    fisheye-проекция f = w/FOV), но ширина объекта — ШИРИНА ВОРОТ из
    регламента (45-60 см, параметр турнира), а не мяч. None при мусорном
    входе. Никогда не бросает.
    """
    try:
        w = float(w_px)
        fw = float(frame_w)
        gw = float(gate_width_cm)
    except (TypeError, ValueError):
        return None
    if not (math.isfinite(w) and math.isfinite(fw) and math.isfinite(gw)):
        return None
    if fw <= 0 or gw <= 0 or w <= 0:
        return None
    return gw * (fw / math.pi) / max(w, 1.0)


# ===================== ФИТ (чистая часть) =====================
def _residuals(a, b, c, d_arr, r_arr):
    """Невязка модели в каждой точке (numpy, без бросков)."""
    with np.errstate(divide="ignore", invalid="ignore"):
        pred = a / (b - r_arr) - c
    return pred - d_arr


def _sse(a, b, c, d_arr, r_arr):
    res = _residuals(a, b, c, d_arr, r_arr)
    res = np.where(np.isfinite(res), res, 1e18)
    return float(np.dot(res, res))


def _gauss_newton_polish(a, b, c, d_arr, r_arr, iters=20):
    """Полировка фита Гауссом-Ньютоном по ИСХОДНОЙ невязке d_pred - d.

    Линеаризованный lstsq минимизирует алгебраизированную невязку, не
    сумму (d_pred - d)^2; для точных данных оба дают одно и то же, для
    шумных полировка заметно точнее. При расхождении (SSE вырос) — шаг
    отбрасывается, вернули лучшее. Дампов нет: 3x3 решается напрямую.
    """
    best = (a, b, c, _sse(a, b, c, d_arr, r_arr))
    for _ in range(iters):
        a0, b0, c0 = best[0], best[1], best[2]
        with np.errstate(divide="ignore", invalid="ignore"):
            denom = b0 - r_arr
            pred = a0 / denom - c0
        res = pred - d_arr
        if not np.all(np.isfinite(res)):
            break
        # J = [d/da, d/db, d/dc] = [1/(b-r), -a/(b-r)^2, -1]
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = 1.0 / denom
        j0 = inv
        j1 = -a0 * inv * inv
        j2 = -np.ones_like(r_arr)
        J = np.column_stack([j0, j1, j2])
        if not np.all(np.isfinite(J)):
            break
        JTJ = J.T @ J + np.eye(3) * 1e-12
        JTr = J.T @ res
        try:
            delta = np.linalg.solve(JTJ, JTr)
        except np.linalg.LinAlgError:
            break
        if not np.all(np.isfinite(delta)):
            break
        cand = (a0 - delta[0], b0 - delta[1], c0 - delta[2])
        sse = _sse(*cand, d_arr, r_arr)
        if sse < best[3]:
            best = (cand[0], cand[1], cand[2], sse)
        else:
            break
    return best[0], best[1], best[2]


def fit_model(points):
    """Подбирает (a, b, c) по точкам [(dist_cm, r_px), ...].

    Возвращает (model_dict | None, report_str). Модель валидируется
    (конечность, убывание, асимптота вне вилки) — мусорные точки дают
    None, а не кривую, которая «сойдёт с ума» на матче.
    """
    pts = []
    for p in points or ():
        try:
            d, r = float(p[0]), float(p[1])
        except (TypeError, ValueError, IndexError):
            return None, "точка %r не (dist_cm, r_px)" % (p,)
        if not (math.isfinite(d) and math.isfinite(r)):
            continue
        if d <= 0 or r <= 0:
            return None, "точка (%g, %g): нужны dist_cm > 0 и r_px > 0" % (d, r)
        pts.append((d, r))
    if len(pts) < MIN_POINTS:
        return None, "точек %d < %d" % (len(pts), MIN_POINTS)

    d_arr = np.array([p[0] for p in pts], dtype=np.float64)
    r_arr = np.array([p[1] for p in pts], dtype=np.float64)
    if float(np.ptp(r_arr)) < 1e-9:
        return None, "все r одинаковые — фит вырожден"

    # Линеаризация: b*d + q - c*r = r*d, q = c*b - a.
    A = np.column_stack([d_arr, np.ones_like(d_arr), -r_arr])
    y = r_arr * d_arr
    try:
        sol, _, rank, _ = np.linalg.lstsq(A, y, rcond=None)
    except np.linalg.LinAlgError as exc:
        return None, "lstsq не сошёлся: %s" % (exc,)
    if rank < 3:
        return None, "система вырождена (rank=%d) — точки слишком похожи" % rank
    b, q, c = (float(v) for v in sol)
    a = c * b - q

    a, b, c = _gauss_newton_polish(a, b, c, d_arr, r_arr)

    r_lo = float(np.min(r_arr))
    r_hi = float(np.max(r_arr))
    err = validate_model(a, b, c, r_lo, r_hi)
    if err is not None:
        return None, err

    rep = _fit_report(a, b, c, pts)
    return {"a": a, "b": b, "c": c, "points": pts}, rep


def _eval_interval(a, b, c, r_lo, r_hi, has_points):
    """Рабочий интервал r для evaluate(): вилка калибровки +-25% размаха,
    ОБРЕЗАННАЯ до безопасной от асимптоты b стороны (оценка не должна
    перепрыгивать полюс даже на разрешённой экстраполяции).

    None = интервала нет (нет точек калибровки — только EPS-деление) либо
    фит сломан (полюс внутри данных)."""
    if not has_points:
        return None
    span = max(1.0, r_hi - r_lo)
    ext = max(EXTRAP_MIN_PX, EXTRAP_FRAC * span)
    lo = r_lo - ext
    hi = r_hi + ext
    pole = max(1.0, 0.05 * span)  # не подходить к полюсу ближе этого
    if r_lo <= b <= r_hi:
        return None  # полюс внутри данных — фит сломан
    if b < r_lo:
        lo = max(lo, b + pole)
    else:  # b > r_hi
        hi = min(hi, b - pole)
    if lo >= hi:
        return None
    return lo, hi


def validate_model(a, b, c, r_lo, r_hi):
    """Проверка правдоподобия модели. None = ok, иначе строка-причина.

    Правила: конечность; a < 0 (строгое убывание d по r: больше ворота =
    ближе; наклон d' = a/(b-r)^2 — знак задаёт a); асимптота b вне вилки
    калибровочных r (иначе часть точек сидит на делении/за ним); модель
    монотонна и конечна на всём рабочем интервале (проверка выборкой).
    """
    if not all(math.isfinite(v) for v in (a, b, c)):
        return "модель содержит NaN/inf"
    if a >= 0:
        return "a=%g >= 0: модель не убывает по r (больше ворота должны быть ближе)" % a
    has_points = r_hi > r_lo
    if has_points and r_lo <= b <= r_hi:
        return "асимптота b=%g внутри вилки калибровки r=[%g..%g]" % (b, r_lo, r_hi)
    # Выборочная проверка: конечность и убывание на всём рабочем интервале.
    interval = _eval_interval(a, b, c, r_lo, r_hi, has_points)
    if has_points and interval is None:
        return "рабочий интервал пуст (полюс слишком близко к вилке r)"
    if interval is not None:
        lo, hi = interval
    else:
        lo, hi = 1.0, 500.0  # модель без точек — проверяем правдоподобный диапазон
    prev = None
    n = 64
    step = max(1e-6, (hi - lo) / n)
    r = lo
    while r <= hi + 1e-9:
        denom = b - r
        if abs(denom) > EPS_DIV:
            d = a / denom - c
            if not math.isfinite(d):
                return "модель даёт NaN/inf при r=%g" % r
            if prev is not None and d > prev + 1e-9:
                return "модель не монотонна (d растёт при r=%g)" % r
            prev = d
        r += step
    return None


def _fit_report(a, b, c, pts):
    """Человекочитаемый отчёт о качестве фита (для калибратора)."""
    lines = []
    worst = 0.0
    sse = 0.0
    for d, r in pts:
        pred = a / (b - r) - c
        res = pred - d
        sse += res * res
        worst = max(worst, abs(res))
        lines.append("  d=%8.2f cm  r=%8.2f px  -> предск. %8.2f  (невязка %+7.2f)"
                     % (d, r, pred, res))
    lines.insert(0, "a=%.6g b=%.6g c=%.6g | остаточная СКО=%.3f cm, макс |невязка|=%.3f cm"
                 % (a, b, c, math.sqrt(sse / max(1, len(pts))), worst))
    return "\n".join(lines)


# ===================== МОДЕЛЬ =====================
class DistModel:
    """Валидированная модель d = a/(b-r) - c с защитой на каждом вызове.

    evaluate() НИКОГДА не бросает и не возвращает мусор: (None, False)
    при любом сомнении. Масштаб кадра калибровки учитывается: радиус
    линеен по кадру, поэтому r_калибр = r_рантайм * min_dim_калибр /
    min_dim_рантайм (Pi 640x360 -> ноутбук 720x1280 портрет = x2).
    """

    def __init__(self, a, b, c, points=(), frame_w=0, frame_h=0):
        self.a = float(a)
        self.b = float(b)
        self.c = float(c)
        pts = []
        for p in points or ():
            try:
                d, r = float(p[0]), float(p[1])
            except (TypeError, ValueError, IndexError):
                continue
            if math.isfinite(d) and math.isfinite(r) and d > 0 and r > 0:
                pts.append((d, r))
        self.points = pts
        try:
            fw, fh = int(frame_w), int(frame_h)
        except (TypeError, ValueError):
            fw = fh = 0
        self.frame_w = fw if fw > 0 else 0
        self.frame_h = fh if fh > 0 else 0
        self.frame_min = min(self.frame_w, self.frame_h) \
            if (self.frame_w and self.frame_h) else 0

        if pts:
            self.r_lo = min(p[1] for p in pts)
            self.r_hi = max(p[1] for p in pts)
        else:
            self.r_lo = self.r_hi = 0.0

        # Рабочий интервал: вилка калибровки +-25% размаха, обрезанная до
        # безопасной от асимптоты стороны (см. _eval_interval). None — только
        # EPS-деление (модель без точек).
        interval = _eval_interval(self.a, self.b, self.c, self.r_lo,
                                  self.r_hi, bool(pts))
        self.r_eval_lo, self.r_eval_hi = \
            (interval if interval is not None else (None, None))

        self.error = validate_model(self.a, self.b, self.c, self.r_lo, self.r_hi)
        self.valid = self.error is None

    @property
    def model_name(self):
        return MODEL_NAME

    def evaluate(self, r_px, frame_min_dim=0):
        """(dist_cm | None, reliable). Гарантия отсутствия мусора.

        - r не конечен/<=0 -> (None, False);
        - |b - r| <= EPS_DIV (деление) -> (None, False);
        - r вне вилки калибровки +-25% -> (None, False) — экстраполяция;
        - d вне 0..500 -> зажимается, reliable=False;
        - NaN/inf на выходе -> (None, False).
        """
        r = float(r_px)
        if not math.isfinite(r) or r <= 0:
            return None, False
        if self.frame_min > 0 and frame_min_dim \
                and frame_min_dim > 0 and int(frame_min_dim) != self.frame_min:
            r = r * self.frame_min / float(frame_min_dim)
        denom = self.b - r
        if not math.isfinite(denom) or abs(denom) <= EPS_DIV:
            return None, False
        if self.r_eval_lo is not None \
                and not (self.r_eval_lo <= r <= self.r_eval_hi):
            return None, False
        d = self.a / denom - self.c
        if not math.isfinite(d):
            return None, False
        reliable = True
        if d < D_MIN_CM or d > D_MAX_CM:
            d = min(max(d, D_MIN_CM), D_MAX_CM)
            reliable = False
        return d, reliable


def model_from_dict(data):
    """DistModel из разобранного JSON (dict). None при любом мусоре.

    version/model проверяются; неизвестные поля игнорируются. Никогда
    не бросает: битый файл не должен ронять запуск Pi.
    """
    if not isinstance(data, dict):
        return None
    try:
        version = int(data.get("version", 0))
    except (TypeError, ValueError):
        return None
    if version != VERSION:
        _warn("distance_calib.json: версия %s != %d — игнорирую"
              % (data.get("version"), VERSION))
        return None
    name = data.get("model", MODEL_NAME)
    if name != MODEL_NAME:
        _warn("distance_calib.json: неизвестная модель %r — игнорирую" % (name,))
        return None
    try:
        a = float(data["a"])
        b = float(data["b"])
        c = float(data["c"])
    except (KeyError, TypeError, ValueError):
        return None
    if not all(math.isfinite(v) for v in (a, b, c)):
        _warn("distance_calib.json: a/b/c не конечны — игнорирую")
        return None
    points = data.get("points") if isinstance(data.get("points"), list) else ()
    return DistModel(a, b, c, points,
                     data.get("frame_w", 0), data.get("frame_h", 0))


def load_model(path=None):
    """Читает distance_calib.json; None если файла нет или он повреждён.

    Битый JSON -> WARN + None (вызывающий уходит в аналитический
    fallback). Никогда не бросает.
    """
    path = path or default_path()
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except Exception as exc:
        _warn("%s повреждён (%s) — работаю без калибровки (аналитическая модель)"
              % (os.path.basename(path), exc))
        return None
    model = model_from_dict(data)
    if model is not None and not model.valid:
        _warn("distance_calib.json: модель отклонена (%s) — работаю без "
              "калибровки (аналитическая модель)" % model.error)
        return None
    return model


def model_to_dict(model, ts=None):
    """JSON-дикт модели (schema контракта; points = [[dist_cm, r_px], ...])."""
    if ts is None:
        ts = time.strftime("%Y-%m-%dT%H:%M:%S")
    return {
        "version": VERSION,
        "model": MODEL_NAME,
        "a": float(model.a),
        "b": float(model.b),
        "c": float(model.c),
        "points": [[float(d), float(r)] for d, r in model.points],
        "frame_w": int(model.frame_w),
        "frame_h": int(model.frame_h),
        "ts": ts,
    }


def save_model_dict(path, data):
    """Атомарная запись dict'а: tmp (та же директория) + fsync + os.replace.

    False при ошибке — старый файл остаётся нетронутым. Никогда не бросает.
    """
    path = path or default_path()
    tmp_path = None
    try:
        payload = json.dumps(data, ensure_ascii=False, indent=1) + "\n"
        tmp_path = "%s.%d.tmp" % (path, os.getpid())
        with open(tmp_path, "w", encoding="utf-8") as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, path)
        return True
    except Exception as exc:
        _warn("не удалось записать %s: %s" % (path, exc))
        try:
            if tmp_path:
                os.remove(tmp_path)
        except OSError:
            pass
        return False


def save_model(path, model):
    """Атомарно пишет DistModel в distance_calib.json."""
    return save_model_dict(path, model_to_dict(model))


# ===================== УДЕРЖАНИЕ (occlusion hold) =====================
class DistHold:
    """Удержание последнего достоверного значения T секунд.

    update(dist, reliable, now): достоверное значение запоминается с
    меткой времени; недостоверное -> предыдущее достоверное, пока
    now - t_достоверного <= T, иначе None. Никогда не бросает.
    """

    def __init__(self, hold_s=DEFAULT_HOLD_S):
        self.hold_s = max(0.0, float(hold_s))
        self.value = None
        self.t = None

    def reset(self):
        self.value = None
        self.t = None

    def held_value(self, now):
        if self.value is not None and self.t is not None \
                and now - self.t <= self.hold_s:
            return self.value
        return None

    def update(self, dist_cm, reliable, now):
        if reliable and dist_cm is not None and math.isfinite(dist_cm):
            self.value = float(dist_cm)
            self.t = float(now)
            return self.value
        return self.held_value(now)


# ===================== ОЦЕНКА НА ВОРОТА (модель + эвристики) =====================
class GateDistEstimator:
    """Дистанция ОДНИХ ворот за кадр: модель/fallback + защита + эвристики
    перекрытия + удержание. Состояние на ворота — держится у вызывающего
    (rpi_elp_cam.GateTracker / unikostyl.compute_track / pi_core GateTracker).

    update() возвращает (dist_cm | None, reliable):
      * свежее обнаружение, все проверки пройдены -> (d, True);
      * заслонённые/подозрительные -> (последнее достоверное в окне T, False)
        либо (None, False), когда окно T истекло;
      * кадр held (маска пуста, позиция держит гистерезис Idea 5) ->
        значение не пересчитывается (радиус тот же), reliable=False.

    update() НИКОГДА не бросает.
    """

    def __init__(self, model=None, hold_s=DEFAULT_HOLD_S,
                 drop_frac=DEFAULT_DROP_FRAC, fill_min=DEFAULT_FILL_MIN,
                 move_frac=DEFAULT_MOVE_FRAC,
                 gate_width_cm=DEFAULT_GATE_WIDTH_CM):
        self.model = model if (model is not None and model.valid) else None
        self.hold_s = max(0.0, float(hold_s))
        self.drop_frac = min(1.0, max(0.0, float(drop_frac)))
        self.fill_min = max(0.0, float(fill_min))
        self.move_frac = max(0.0, float(move_frac))
        self.gate_width_cm = float(gate_width_cm)
        self._hold = DistHold(self.hold_s)
        self._ref_radius = None    # последний ДОСТОВЕРНЫЙ радиус (для drop)
        self._ref_cx = None
        self._ref_cy = None

    def set_model(self, model):
        """Сменить модель (перезагрузка калибровки) — состояние сбрасывается."""
        self.model = model if (model is not None and model.valid) else None
        self.reset()

    def reset(self):
        """Новый цвет ворот / рестарт: удержание и эталоны обнуляются."""
        self._hold.reset()
        self._ref_radius = None
        self._ref_cx = None
        self._ref_cy = None

    def _move_px(self, cx, cy, frame_min_dim):
        if self._ref_cx is None or cx is None or cy is None:
            return float("inf")
        try:
            dx = float(cx) - self._ref_cx
            dy = float(cy) - self._ref_cy
        except (TypeError, ValueError):
            return float("inf")
        if not (math.isfinite(dx) and math.isfinite(dy)):
            return float("inf")
        return math.sqrt(dx * dx + dy * dy)

    def update(self, radius, w_px, h_px, area_px, cx, cy, held, now,
               frame_min_dim=0, frame_w=0):
        """Один кадр одних ворот. Все входы валидируются внутри."""
        # Кадр удержан гистерезисом Idea 5: маска пуста, размер не
        # подтверждён -> dist недостоверен, значение держится (оно и так
        # совпадает с последним принятым — радиус held не меняется).
        if held:
            return self._hold.update(None, False, now), False

        try:
            r = float(radius)
        except (TypeError, ValueError):
            r = float("nan")
        if not math.isfinite(r) or r <= 0:
            return self._hold.update(None, False, now), False

        move = self._move_px(cx, cy, frame_min_dim)
        move_max = self.move_frac * float(frame_min_dim) \
            if frame_min_dim and frame_min_dim > 0 else 0.0

        # Эвристика 1: ворота «на месте» (центроид не уехал), но внезапно
        # стали заметно меньше — кто-то заслонил (видимый размер занижен,
        # дистанция была бы завышена).
        occluded = False
        if self.drop_frac > 0 and self._ref_radius is not None:
            if r < self.drop_frac * self._ref_radius and move <= move_max:
                occluded = True

        # Эвристика 2: «дырки» в объединённом боксе — куски ворот слиты
        # через тело противника / объект рыхлый: размер бокса не равен
        # размеру ворот.
        fill_ok = True
        if self.fill_min > 0:
            try:
                w = float(w_px)
                h = float(h_px)
                ar = float(area_px)
            except (TypeError, ValueError):
                fill_ok = False
            else:
                if w > 0 and h > 0 and math.isfinite(ar) and ar > 0:
                    if ar / (w * h) < self.fill_min:
                        fill_ok = False
                else:
                    fill_ok = False

        if occluded or not fill_ok:
            return self._hold.update(None, False, now), False

        # Размер -> дистанция: калиброванная модель либо аналитический fallback.
        if self.model is not None:
            d, ok = self.model.evaluate(r, frame_min_dim)
        else:
            d = analytic_distance_cm(w_px, frame_w, self.gate_width_cm)
            if d is None:
                ok = False
            elif d < D_MIN_CM or d > D_MAX_CM:
                d = min(max(d, D_MIN_CM), D_MAX_CM)
                ok = False
            else:
                ok = True

        out = self._hold.update(d, ok, now)
        if ok:
            self._ref_radius = r
            try:
                self._ref_cx = float(cx)
                self._ref_cy = float(cy)
            except (TypeError, ValueError):
                self._ref_cx = self._ref_cy = None
        return out, ok
