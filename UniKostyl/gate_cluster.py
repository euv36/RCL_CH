# -*- coding: utf-8 -*-
"""gate_cluster.py — Idea 5: blob clustering and loss hysteresis for gates.

Проблема (PROMPT_EN.md §5, Idea 5): после захвата цвета маска может
рассыпаться на куски в разных местах кадра («майка»/посторонний предмет),
а воротами противник может разрезать на части. Требуется: близкие куски
сливать в ОДНИ ворота, мелочь и далёкие «чужие» объекты отбрасывать
(на один цвет — максимум один объект ворот) и держать гистерезис, чтобы
робот не «сходил с ума» на мерцании маски.

Конвейер (каждый кадр, на каждый цвет):
  1. mask (inRange) — готовит вызывающий (rpi_elp_cam.GateTracker /
     unikostyl.compute_track);
  2. detect_gate(): быстрый отсев по суммарной площади (countNonZero —
     НЕ findNonZero: ничего не аллоцирует, только счётчик), затем
     connectedComponentsWithStats -> статистика компонент (центроид,
     площадь, бокс — то, что нужно, без findNonZero/boundingRect);
  3. build_gate_object() — ЧИСТАЯ функция (numpy без cv2, зеркалится
     1-в-1 в pi_core/src/gate_cluster.cpp):
       a) отсев мелочи: компонента < min_area_px — не ворота;
       b) слияние близких: компоненты с зазором боксов <= merge_gap_px
          объединяются (union-find) — «тело противника» между кусками;
       c) выбор якоря: кластер, ближайший к прежней позиции ворот
          (has_prev), иначе самый большой;
       d) отсев «майки»: кластеры дальше max_stray_px от центроида
          якоря отбрасываются;
       e) итог: один объект — центроид (взвешенный по площадям, это
          ТОЧНЫЙ центроид объединения компонент), площадь, объединённый
          бокс и радиус.
     Радиус = описанная окружность объединённого бокса кластера
     (0.5*sqrt(w^2+h^2)). Это верхняя оценка minEnclosingCircle по
     пикселям: для компактных/прямоугольных blob'ов совпадает с ней
     с точностью до формы, для «круглых» завышает (~sqrt(2)/2 против
     0.5 диаметра). Выбрано намеренно: детерминированно, дёшево и
     БИТ-В-БИТ совпадает между Python и C++ (sqrt — корректно
     округляемая операция IEEE-754), а калибровка дистанции (Idea 6)
     подгоняет константы под фактическую меру радиуса.
  4. GateStability.update() — гистерезис (чистый класс, зеркалится в C++):
       - ворота считаются потерянными только после N подряд пустых
         кадров (lost_frames, по умолчанию 5) — и только тогда публикуется
         «нет ворот» (серийная строка G<id>,-1,0 уходит ОДИН раз, формат
         не меняется, меняется только момент);
       - новое обнаружение принимается после M подряд кадров
         (confirm_frames, по умолчанию 3);
       - пока ворота «найдены», в кадре предпочитается кластер,
           ближайший к прежней позиции; новый далёкий объект принимается
           только после M подтверждённых кадров (без промежуточного
           «потеряны», если успел подтвердиться до N пустых кадров);
       - в окне грации (пустые кадры < N) публикуется последняя
         принятая позиция с пометкой held=True — серийная линия не
         дёргается (угол не меняется — порог 3° не превышен);
       - исключение: ПЕРВОЕ в жизни трекера обнаружение принимается
         сразу (старое поведение без гистерезиса; защищать нечего —
         ни прежней позиции, ни отправленного -1 ещё нет).

Замечания по производительности:
  - connectedComponentsWithStats по цене ~= cv2.moments по полной маске,
    который этот код заменяет (замер на 640x360: ~1.7 мс оба); чистая
    часть — O(кандидатов^2) с numpy-матрицей зазоров, кандидаты после
    отсева мелочи обычно единицы; при лавине компонент (>256) берутся
    256 крупнейших (предохранитель).
  - запрещённые горячие точки не возвращаются: findNonZero/boundingRect
    не используются, попиксельных Python-циклов нет.

Модуль деплоится на Pi рядом с rpi_elp_cam.py (pscp, как gate_store.py).
"""

import math

import numpy as np

try:
    import cv2
except ImportError:  # чистая логика (parity-тесты) работает и без cv2
    cv2 = None


# -------- параметры по умолчанию (PROMPT §5 Idea 5) --------
MERGE_GAP_FRAC = 0.025   # зазор слияния, доля ширины кадра (~2-3%)
MAX_STRAY_FRAC = 0.10    # радиус «своих» кусков / непрерывности трека, доля ширины
OPEN_KERNEL = 3          # ядро морфологического OPEN (нечётное, 1 = выкл)
LOST_FRAMES = 5          # пустых кадров подряд -> ворота потеряны (G<id>,-1,0)
CONFIRM_FRAMES = 3       # кадров подряд -> новое обнаружение принято
MAX_CANDIDATES = 256     # предохранитель: сколько крупнейших компонент обрабатывать
MAX_CLUSTER_ASPECT = 6.5  # max(w,h)/min(w,h): стойки лампы и изолента не ворота
MIN_CLUSTER_SIDE_FRAC = 0.008  # доля меньшей стороны кадра, защита от сплошных линий


def resolve_params(width, height, merge_gap_px=None, min_area_px=None,
                   max_stray_px=None, open_kernel=OPEN_KERNEL,
                   lost_frames=LOST_FRAMES, confirm_frames=CONFIRM_FRAMES,
                   max_aspect=None, min_cluster_side_px=None):
    """Заполняет не заданные (None/<=0) параметры масштабом от ширины кадра.

    min_area_px остаётся 0.0 (= фильтр мелочи выключен), если вызывающий
    не передал значение: у Pi умолчанием служит area_min
    (AREA_MIN_PIXELS_1080P, масштабируется с разрешением), у ноутбука —
    unikostyl.TARGET_MIN_AREA. merge_gap/max_stray по умолчанию — доли
    ширины кадра (см. константы выше). open_kernel принудительно нечётный.
    """
    w = max(1, int(width))
    h = max(1, int(height))
    merge = float(merge_gap_px) if merge_gap_px and float(merge_gap_px) > 0 \
        else float(round(MERGE_GAP_FRAC * w))
    stray = float(max_stray_px) if max_stray_px and float(max_stray_px) > 0 \
        else float(round(MAX_STRAY_FRAC * w))
    k = int(open_kernel) if open_kernel else 1
    if k < 1:
        k = 1
    if k % 2 == 0:
        k += 1
    min_area = float(min_area_px) if min_area_px and float(min_area_px) > 0 \
        else 0.0
    aspect = float(max_aspect) if max_aspect and float(max_aspect) > 0 \
        else float(MAX_CLUSTER_ASPECT)
    min_side = (float(min_cluster_side_px) if min_cluster_side_px and float(min_cluster_side_px) > 0
                else float(round(MIN_CLUSTER_SIDE_FRAC * min(w, h))))
    return {
        "width": w,
        "height": h,
        "merge_gap_px": merge,
        "min_area_px": min_area,
        "max_stray_px": stray,
        "open_kernel": k,
        "lost_frames": max(1, int(lost_frames)),
        "confirm_frames": max(1, int(confirm_frames)),
        "max_aspect": aspect,
        "min_cluster_side_px": min_side,
    }


# ================= ЧИСТАЯ ЧАСТЬ (зеркалится в pi_core 1-в-1) =================

def rect_gap(x1, y1, w1, h1, x2, y2, w2, h2):
    """Евклидов зазор между прямоугольниками (0, если перекрываются).

    sqrt от точных квадратов (не hypot): sqrt корректно округляется по
    IEEE-754, поэтому Python и C++ дают бит-в-бит один результат и решение
    «сливать/не сливать» на грани merge_gap_px совпадает.
    """
    dx = max(0.0, max(x1, x2) - min(x1 + w1, x2 + w2))
    dy = max(0.0, max(y1, y2) - min(y1 + h1, y2 + h2))
    return math.sqrt(dx * dx + dy * dy)


def angle_deg(cx, cy, cx0, cy0):
    """int(math.degrees(math.atan2(cy0 - cy, cx - cx0)) % 360); 0°=вправо, CCW."""
    return int(math.degrees(math.atan2(cy0 - cy, cx - cx0)) % 360)


def cluster_gate_like(cluster, params):
    """Reject a merged blob that cannot be a gate (wires/lamp posts/tape)."""
    w = float(cluster.get("w", 0.0))
    h = float(cluster.get("h", 0.0))
    small = min(w, h)
    if small < float(params.get("min_cluster_side_px", 0.0)):
        return False
    if small <= 0.0:
        return False
    return max(w, h) / small <= float(params.get("max_aspect", MAX_CLUSTER_ASPECT))


def build_gate_object(comps, params, prev_x=None, prev_y=None,
                      want_members=False):
    """Собирает ОДИН объект ворот из компонент CC (чистая функция).

    comps: список кортежей (cx, cy, area, x, y, w, h) — центроид, площадь
    и бокс каждой компоненты (из connectedComponentsWithStats). prev_x/
    prev_y — прежняя позиция ворот (публикуемый центроид; None = трекер
    ещё не нашёл ворота / потерял их).

    Шаги: cap кандидатов -> слияние по зазору -> якорь -> отсев «майки»
    -> итоговый объект. Возвращает dict:
      {"cx", "cy", "area", "x", "y", "w", "h", "radius",
       "members": [(cx, cy, area), ...] при want_members}
    или None, если компонент нет.
    """
    n = len(comps)
    if n == 0:
        return None

    # 1. Предохранитель: лавина компонент на шумной маске. Берём крупнейшие
    #    MAX_CANDIDATES по (площадя desc, индекс asc) и возвращаем к
    #    возрастанию индексов — детерминированно и одинаково с C++.
    if n > MAX_CANDIDATES:
        order = sorted(range(n), key=lambda i: (-comps[i][2], i))[:MAX_CANDIDATES]
        keep = sorted(order)
    else:
        keep = list(range(n))
    m = len(keep)

    # 2. Слияние близких компонент: матрица зазоров боксов (numpy, без
    #    попиксельных циклов) + union-find по парам с зазором <= merge_gap.
    l = np.empty(m, dtype=np.float64)
    t = np.empty(m, dtype=np.float64)
    r = np.empty(m, dtype=np.float64)
    b = np.empty(m, dtype=np.float64)
    for j in range(m):
        c = comps[keep[j]]
        l[j] = c[3]
        t[j] = c[4]
        r[j] = c[3] + c[5]
        b[j] = c[4] + c[6]
    dx = np.maximum(0.0, np.maximum(l[:, None], l[None, :]) -
                    np.minimum(r[:, None], r[None, :]))
    dy = np.maximum(0.0, np.maximum(t[:, None], t[None, :]) -
                    np.minimum(b[:, None], b[None, :]))
    mg = float(params["merge_gap_px"])
    pair = (dx * dx + dy * dy) <= mg * mg
    pair = np.triu(pair, k=1)  # пары a<b, диагональ не нужна

    parent = list(range(m))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for a, bb in np.argwhere(pair).tolist():
        ra, rb = find(a), find(bb)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    # 3. Агрегаты кластеров. Порядок кластеров — по минимальному индексу
    #    члена (детерминирован, не зависит от выбора корня union-find).
    groups = {}
    for j in range(m):
        groups.setdefault(find(j), []).append(j)
    cluster_lists = sorted(groups.values(), key=min)

    aggs = []
    for members_local in cluster_lists:
        idxs = [keep[j] for j in members_local]
        area = 0.0
        sx = 0.0
        sy = 0.0
        x0 = float("inf")
        y0 = float("inf")
        x1 = -float("inf")
        y1 = -float("inf")
        for i in idxs:
            c = comps[i]
            a = c[2]
            area += a
            sx += a * c[0]
            sy += a * c[1]
            if c[3] < x0:
                x0 = c[3]
            if c[4] < y0:
                y0 = c[4]
            if c[3] + c[5] > x1:
                x1 = c[3] + c[5]
            if c[4] + c[6] > y1:
                y1 = c[4] + c[6]
        # Центроид объединения непересекающихся компонент = взвешенное по
        # площади среднее центроидов (ТОЧНО, второй cv2.moments не нужен).
        aggs.append({
            "cx": sx / area,
            "cy": sy / area,
            "area": area,
            "x": x0,
            "y": y0,
            "w": x1 - x0,
            "h": y1 - y0,
            "members": idxs,
        })

    # 3b. Shape guard on merged clusters: thin tape/lamp posts are valid
    # colour blobs but not gate objects. Applied AFTER merge so split pieces
    # of one real goal can still pass.
    aggs = [agg for agg in aggs if cluster_gate_like(agg, params)]
    if not aggs:
        return None

    # 4. Якорь: при активном треке — ближайший к прежней позиции ворот
    #    (ничья -> большая площадь -> меньший индекс); без трека — самый
    #    большой кластер.
    if prev_x is not None:
        px = float(prev_x)
        py = float(prev_y)
        best = min(
            range(len(aggs)),
            key=lambda k: (
                (aggs[k]["cx"] - px) ** 2 + (aggs[k]["cy"] - py) ** 2,
                -aggs[k]["area"],
                k,
            ),
        )
    else:
        best = min(
            range(len(aggs)),
            key=lambda k: (-aggs[k]["area"], k),
        )
    anchor = aggs[best]

    # 5. Отсев «майки»: кластеры дальше max_stray_px от центроида якоря —
    #    чужие (PROMPT §5: «компонента дальше max_stray_px от центроида
    #    основных кусков — посторонний объект — отбросить»).
    stray = float(params["max_stray_px"])
    kept = [anchor]
    for k, agg in enumerate(aggs):
        if k == best:
            continue
        d = math.sqrt(
            (agg["cx"] - anchor["cx"]) ** 2 + (agg["cy"] - anchor["cy"]) ** 2
        )
        if d <= stray:
            kept.append(agg)

    # 6. Итоговый объект: объединение якоря и прикипевших кластеров.
    area = 0.0
    sx = 0.0
    sy = 0.0
    x0 = float("inf")
    y0 = float("inf")
    x1 = -float("inf")
    y1 = -float("inf")
    members = []
    for agg in kept:
        area += agg["area"]
        sx += agg["area"] * agg["cx"]
        sy += agg["area"] * agg["cy"]
        if agg["x"] < x0:
            x0 = agg["x"]
        if agg["y"] < y0:
            y0 = agg["y"]
        if agg["x"] + agg["w"] > x1:
            x1 = agg["x"] + agg["w"]
        if agg["y"] + agg["h"] > y1:
            y1 = agg["y"] + agg["h"]
        if want_members:
            # Кортежи (cx, cy, area) — для кругового среднего угла в RING.
            members.extend(
                (comps[i][0], comps[i][1], comps[i][2])
                for i in agg["members"]
            )

    w = x1 - x0
    h = y1 - y0
    # Радиус = описанная окружность объединённого бокса (верхняя оценка
    # minEnclosingCircle; см. докстринг модуля — паритет Python/C++).
    radius = 0.5 * math.sqrt(w * w + h * h)
    det = {
        "cx": sx / area,
        "cy": sy / area,
        "area": area,
        "x": x0,
        "y": y0,
        "w": w,
        "h": h,
        "radius": radius,
        "n_clusters": len(kept),
        "n_components": sum(len(a["members"]) for a in kept),
    }
    if want_members:
        det["members"] = members
    return det


# ================= УРОВЕНЬ МАСКИ (cv2) =================

def detect_gate(mask, params, prev_x=None, prev_y=None, labels=None,
                want_members=False):
    """Кластеризация одной бинарной маски (0/255) в объект ворот или None.

    mask — уже после морфологии (вызывающий делает inRange + OPEN/CLOSE).
    labels — предвыделенный буфер int32 той же формы (переиспользуется,
    без аллокаций на кадр). Чистая часть — build_gate_object().
    """
    if cv2 is None:
        raise RuntimeError("gate_cluster: OpenCV (cv2) недоступен")

    # Быстрый отсев по суммарной площади: countNonZero — это НЕ запрещённый
    # findNonZero (тот аллоцирует массив координат на каждый пиксель),
    # здесь — только счётчик, без аллокаций, ~0.02 мс на 640x360.
    total = float(cv2.countNonZero(mask))
    if total < float(params["min_area_px"]):
        return None

    if labels is not None and labels.shape == mask.shape \
            and labels.dtype == np.int32:
        num, _lab, stats, cents = cv2.connectedComponentsWithStats(
            mask, labels, None, None, 8, cv2.CV_32S)
    else:
        num, _lab, stats, cents = cv2.connectedComponentsWithStats(
            mask, connectivity=8, ltype=cv2.CV_32S)

    min_area = float(params["min_area_px"])
    if min_area > 0:
        keep_idx = np.nonzero(stats[1:, cv2.CC_STAT_AREA] >= min_area)[0] + 1
    else:
        keep_idx = np.arange(1, num)
    if keep_idx.size == 0:
        return None

    comps = []
    for i in keep_idx.tolist():
        comps.append((
            float(cents[i, 0]),
            float(cents[i, 1]),
            float(stats[i, cv2.CC_STAT_AREA]),
            float(stats[i, cv2.CC_STAT_LEFT]),
            float(stats[i, cv2.CC_STAT_TOP]),
            float(stats[i, cv2.CC_STAT_WIDTH]),
            float(stats[i, cv2.CC_STAT_HEIGHT]),
        ))
    return build_gate_object(comps, params, prev_x, prev_y, want_members)


# ================= ГИСТЕРЕЗИС (зеркалится в pi_core 1-в-1) =================

class GateStability:
    """Гистерезис «не сходить с ума» для одного цвета ворот (чистый класс).

    Состояния и переходы (см. докстринг модуля):
      - найдено: кластер у прежней позиции принимается сразу; далёкий —
        копится в pending и вытесняет трек после confirm_frames кадров
        подряд; пустые кадры копятся в empty_count и после lost_frames
        дают «потеряно» (lost_event=True ровно на одном кадре);
      - грация: пока empty_count < lost_frames, публикуется последняя
        принятая позиция (held=True);
      - потеряно: новое обнаружение принимается после confirm_frames
        кадров подряд у той же (дрейфующей) позиции;
      - первое в жизни обнаружение принимается сразу (ever_found=False).
    """

    def __init__(self, lost_frames=LOST_FRAMES, confirm_frames=CONFIRM_FRAMES,
                 jump_px=64.0):
        self.lost_frames = max(1, int(lost_frames))
        self.confirm_frames = max(1, int(confirm_frames))
        self.jump_px = float(jump_px)
        self.reset()

    def reset(self):
        self.found = False          # публикуемое состояние
        self.ever_found = False     # было ли хотя бы одно принятое обнаружение
        self.empty_count = 0        # пустых кадров подряд
        self.held = None            # последний принятый объект (dict)
        self.pending = None         # кандидат на смену/появление (dict)
        self.pending_count = 0      # сколько кадров подряд виден кандидат
        self.lost_event = False     # True ровно на кадре объявления потери

    @staticmethod
    def _dist(det_a, det_b):
        return math.sqrt(
            (det_a["cx"] - det_b["cx"]) ** 2 + (det_a["cy"] - det_b["cy"]) ** 2
        )

    def _publish(self, held=False):
        if self.held is None:
            return None
        if held:
            out = dict(self.held)
            out["held"] = True
            return out
        return self.held

    def _accept(self, det):
        self.held = det
        self.found = True
        self.ever_found = True
        self.empty_count = 0
        self.pending = None
        self.pending_count = 0

    def update(self, det):
        """Один кадр. det — dict из build_gate_object/detect_gate или None
        (пусто). Возвращает публикуемый объект (dict) или None."""
        self.lost_event = False

        if det is None:
            self.pending = None
            self.pending_count = 0
            if self.found:
                self.empty_count += 1
                if self.empty_count >= self.lost_frames:
                    # Потеря объявлена: публикуем None; серийная строка
                    # G<id>,-1,0 уйдёт ОДИН раз (handle_serial).
                    self.found = False
                    self.lost_event = True
                    return None
                # грация: держим последнюю позицию
                return self._publish(held=True)
            return None

        # Есть обнаружение.
        if self.found and self.held is not None \
                and self._dist(det, self.held) <= self.jump_px:
            # Тот же объект (непрерывность трека) — принимаем сразу.
            self._accept(det)
            return self._publish()

        # Новый/далёкий кандидат: копим M подряд кадров рядом с НЕЙ
        # (кандидат может дрейфовать — сравниваем с его последней позицией).
        if self.pending is not None \
                and self._dist(det, self.pending) <= self.jump_px:
            self.pending_count += 1
            self.pending = det
        else:
            self.pending = det
            self.pending_count = 1

        if not self.found:
            if not self.ever_found:
                # Старт трекера: принимать сразу — прежней позиции нет,
                # защищать нечего (и старое поведение тестов сохраняется).
                self._accept(det)
                return self._publish()
            if self.pending_count >= self.confirm_frames:
                self._accept(det)
                return self._publish()
            return None

        # Ворота «найдены», но кадр не содержит их у прежней позиции:
        # объект не виден -> копим пустоту (грация/потеря), кандидат ждёт.
        self.empty_count += 1
        if self.pending_count >= self.confirm_frames:
            # Стабильная замена: переключаемся БЕЗ промежуточной потери.
            self._accept(det)
            return self._publish()
        if self.empty_count >= self.lost_frames:
            self.found = False
            self.lost_event = True
            return None
        return self._publish(held=True)

    def held_xy(self):
        """Прежняя позиция для предпочтения якоря (None, если её нет)."""
        if self.found and self.held is not None:
            return self.held["cx"], self.held["cy"]
        return None
