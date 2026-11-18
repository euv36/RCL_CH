# -*- coding: utf-8 -*-
"""radar_panel.py — Idea 7: радар («кружочек») как включаемая панель в главном UI.

Как reference-картинка пользователя: чёрный диск с тонким ободом, светлый
круг в центре = зона корпуса робота (совместлена с Idea 8 self-маской),
настроенные ворота — дуги своего цвета (жёлтый/синий/оранжевый, TRACK_COLORS)
на своих углах вокруг центра, текущая цель (выбор TARGET G1..G3) —
круг-маркер с линией от центра и подписями "angle°" и "r = N px → M cm".

Источники данных — СТРОГО те же, что у трекинга (правило консистентности):
  * режим Pi: разобранный multipart-статус видео 5555. ПАРСИНГ НЕ ДУБЛИРУЕТСЯ:
    радар синхронизируется с виджетом панели статуса (sync_from_status_panel)
    и берёт готовый нормализованный dict из status_panel.parse_status_payload
    (gates/angle/area/dist_cm/dist_reliable/role — неизвестные поля уже
    проброшены парсером);
  * локальный режим: set_local_results(track_results) — тот же фид, что ест
    панель статуса (unikostyl.main_loop_frame).
  * RING-режим (fisheye_mode == 2): угол приходит из семантики x-позиции
    полосы, уже посчитанной трекингом (круговое среднее в compute_track) —
    радар НИКАКИХ углов сам не считает (никакого atan2 тут нет), только
    отображает полученные числа. 0° = вправо, против часовой — та же
    конвенция, что у atan2-углов и serial G<id>,<angle>,<area>.

Выбор источника — как в status_panel: статус Pi считается свежим
PI_STATUS_STALE_S секунд (2 с); протухший статус -> честный возврат к
локальному трекингу (тег "stale" в шапке).

Роли (Idea 9): "ours"/"opponent" -> тонкая зелёная/красная дужка-бейдж под
дугой ворот. В режиме Pi роль читается из поля "role" статус-JSON (приходит
только когда задана — «unset» == отсутствие ключа, инвариант Идеи 9), в
локальном — из set_roles (cv_kostyl feeds saved_gates через панель статуса).

Дистанция (Idea 6): поле "dist_cm"/"dist_reliable" в статусе (или rec
локального трекинга) подхватывается АВТОМАТИЧЕСКИ — подпись цели меняется с
"r = N px → —" на "r = N px → M cm" (как в статус-панели: показывается только
достоверная дистанция; отсутствие dist_reliable = достоверная). Радиальная
позиция маркера цели — грубая визуальная подсказка: dist_cm / RADAR_MAX_CM,
иначе r_px / (min(w,h)/2) рабочего кадра, иначе фиксированная. Точные числа —
в подписи, диск не претендует на калиброванную шкалу.

Производительность: панель скрыта -> draw() выходит сразу (цена ~0).
Статика (фон панели, диск, обод, тики, зона корпуса, румбы) кэшируется в
Surface и перестраивается только при смене размера/параметров зоны; текст
кэшируется (bounded dict). Ничего попиксельного, ничего на самом кадре —
панель рисует только себя. Замер цены: out/radar_check.py (< 0.5 мс/кадр).

Idea 10 (привязка к полю): set_estimated() принимает «last known»
направления из field_map.json; ворота, которых сейчас НЕТ в кадре (не
найдены, но настроены), рисуются приглушённой дугой на сохранённом угле,
подпись цели получает пометку "(est)". Живое измерение ВСЕГДА сильнее
привязки. Точка интеграции — _gate_data() (углы потребляются как пришли,
без пересчёта). Без привязки (пустая карта) поведение прежнее.

Idea 11 (граница поля, советчик): set_edge_sectors() принимает
подтверждённые гистерезисом опасные сектора (field_boundary.py) — они
рисуются красными дугами у обода + тег "EDGE" в шапке. Это ТОЛЬКО
отображение: детектор ничего не блокирует (правило PROMPT_EN §4).
Флагов нет -> ни одной операции отрисовки (цена нулевая).

Модуль ничего не знает про unikostyl/cv_kostyl (как status_panel) — только
сеттеры, поэтому тестируется без камеры и без живого Pi.

# UNVERIFIED ON HW: живой Pi-стрим (multipart 5555) и живая камера — радар
# проверен только на синтетике (out/radar_check.py, SDL dummy) до деплоя на Pi.
"""

import math
import time

import pygame

from widgets import Widget

# Свежесть статуса Pi и хелперы — ОДИН источник с панелью статуса (Idea 2):
# никаких своих констант «старости» и своих парсеров числа/роли.
try:
    from status_panel import PI_STATUS_STALE_S, _safe_float, _norm_role
except Exception:  # pragma: no cover — status_panel всегда рядом; страховка
    PI_STATUS_STALE_S = 2.0

    def _safe_float(val):
        try:
            if isinstance(val, bool):
                return None
            return float(val)
        except (TypeError, ValueError):
            return None

    def _norm_role(value):
        return value if value in ("ours", "opponent") else None


# --- стиль (тёмная панель-прибор: контраст с светлым UI) ---
PANEL_BG = (30, 30, 30)
PANEL_BORDER = (12, 12, 12)
HEADER_TXT = (200, 200, 200)
FOOTER_TXT = (220, 220, 220)
DISC_BG = (8, 8, 8)          # чёрный диск
RIM = (160, 160, 160)        # тонкий обод
TICK = (70, 70, 70)          # тики обода / румбы
BODY_ZONE = (198, 198, 198)  # светлый круг — зона корпуса робота (Idea 8)
BODY_BORDER = (95, 95, 95)
BODY_RING = (120, 120, 120)  # внешняя граница поиска (self_mask r2)
MARKER_TXT = (245, 245, 245)  # подпись угла у маркера цели
MARKER_TXT_ON_BODY = (45, 45, 45)  # та же подпись над светлой зоной корпуса
TAG_PI = (0, 170, 90)        # тег "pi" (свежий статус) — как OK_GREEN панели
TAG_STALE = (210, 60, 45)
TAG_LOCAL = (140, 140, 140)

# Idea 9: цвета бейджей роли (та же семантика, что в панели статуса).
ROLE_OURS = (0, 170, 90)
ROLE_OPPONENT = (210, 60, 45)

# Idea 11: цвет дуг опасных секторов границы поля (советчик, dim-красный).
EDGE_ARC = (170, 45, 35)

# Полуширина дуги ворот на ободе, градусы (видимая «дуга», не точка).
ARC_SPAN_DEG = 10.0
ROLE_SPAN_DEG = 14.0
# Отступы радиусов от обода: дуги ворот и бейджи роли (доля/пиксели от R).
ARC_RADIUS_INSET = 7
ROLE_RADIUS_INSET = 15
# Радиусальная позиция маркера цели без данных о дистанции (доля R).
DEFAULT_MARKER_FRAC = 0.72
# Грубая шкала дистанции на радиусе диска (для подсказки, не для измерений).
RADAR_MAX_CM = 250.0
# Зона корпуса по умолчанию (пока unikostyl не скормил SELF_MASK Идеи 8).
DEFAULT_BODY_FRAC = 0.25


def _clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def marker_radius(R):
    """Радиус круга-маркера цели для диска радиусом R (масштабируется)."""
    return max(5, R // 9)


class RadarPanel(Widget):
    """Радар-панель (Idea 7). Только отображение: клики проходят насквозь.

    Данные подаются извне (как у StatusPanel):
      * set_local_results(track_results) — фид локального трекинга unikostyl;
      * sync_from_status_panel(status_panel_widget) — готовый разобранный
        статус Pi / источник видео / роли (парсинг один — в status_panel);
      * set_roles(roles) — роли локального режима (обычно не нужна: роли
        приходят через sync_from_status_panel);
      * set_estimated(est_map) — Idea 10: «last known» направления из
        привязки поля (field_map.json), для ворот вне вида;
      * set_edge_sectors(flags) — Idea 11: опасные сектора границы
        (советчик field_boundary.py, только отображение);
      * on_pi_status(status) / on_video_source(kind) — прямой фид (тесты);
      * set_target_gate(gid) — выбор цели TARGET G1..G3 (1..3);
      * set_body_zone(enabled, frac_r, frac_r2) — зона корпуса (Idea 8
        SELF_MASK: доли кадра -> доли радиуса диска, r/0.5);
      * set_frame_size(w, h) — размер рабочего кадра трекинга (для доли
        r_px на радиусе);
      * set_visible(v) / toggle() — кнопка RADAR в unikostyl.
    """

    def __init__(self, screen, rect, gate_colors):
        super().__init__(screen, rect, block_click=False)
        self.rect = pygame.Rect(rect)
        self.gate_colors = tuple(gate_colors)  # RGB-кортежи, как TRACK_COLORS
        # Приглушённые цвета дуг (гистерезис держит прежнее положение —
        # «est»-семантика Идеи 10 на будущее).
        self._dim_colors = tuple(
            tuple(int(0.45 * c + 0.55 * 120) for c in col)
            for col in self.gate_colors
        )
        self.font_small = pygame.font.Font(None, 16)
        self.font_marker = pygame.font.Font(None, 18)

        self.visible = False  # включается кнопкой RADAR (не захламлять UI)
        self.pi_status = None       # нормализованный статус Pi (не парсим сами)
        self.local_results = []     # track_results из unikostyl
        self.roles = {}             # {gate_id(int): "ours"|"opponent"}
        self.estimated = {}         # Idea 10: {gate_id(int): {angle_deg, dist_cm}}
        self.edge_flags = []        # Idea 11: подтверждённые опасные сектора
        self.source_kind = "local"  # 'local' | 'pi'
        self.target_gate = 1        # выбор TARGET G1..G3
        self.body_frac = DEFAULT_BODY_FRAC
        self.body_frac2 = 0.0       # внешняя граница поиска (self_mask r2)
        self._frame_min = 0         # min(w, h) рабочего кадра трекинга

        self._bg = None             # кэш статики (панель+диск+зона корпуса)
        self._bg_key = None
        self._geo = None            # (cx, cy, R, footer_y) внутри панели
        self._text_cache = {}       # bounded: (id(font), color, text) -> Surface

    # ---------- видимость ----------
    def set_visible(self, flag):
        self.visible = bool(flag)

    def toggle(self):
        self.visible = not self.visible
        return self.visible

    # ---------- данные ----------
    def set_local_results(self, track_results):
        self.local_results = list(track_results or [])

    def set_roles(self, roles):
        """Роли локального режима {gate_id: "ours"|"opponent"}; мусор — долой."""
        if not isinstance(roles, dict):
            roles = {}
        clean = {}
        for gid, role in roles.items():
            role = _norm_role(role)
            if role is not None:
                try:
                    clean[int(gid)] = role
                except (TypeError, ValueError):
                    continue
        self.roles = clean

    def set_estimated(self, est_map):
        """Idea 10: «last known» направления из привязки поля (field_map.json).

        est_map: {gid: {"angle_deg": float, "dist_cm": float|None}} — весь
        набор записей привязки (unikostyl._estimated_directions()). Радар
        использует запись ТОЛЬКО когда ворота сейчас не найдены, но
        настроены (в _gate_data): живое измерение всегда сильнее привязки.
        Мусор/чужие id отбрасываются; {} = привязки нет (старое поведение).
        """
        if not isinstance(est_map, dict):
            est_map = {}
        clean = {}
        for gid, rec in est_map.items():
            try:
                gid = int(gid)
            except (TypeError, ValueError):
                continue
            if isinstance(rec, dict) and _safe_float(rec.get("angle_deg")) is not None:
                clean[gid] = rec
        self.estimated = clean

    def set_edge_sectors(self, flags):
        """Idea 11: подтверждённые опасные сектора границы (список bool).

        Пустой/None -> [] (ни одного обращения к отрисовке — цена нулевая).
        Панель только отображает совет field_boundary.py; ничего не
        блокирует (adviser-only, PROMPT_EN §5 Idea 11 / §4).
        """
        if not flags:
            self.edge_flags = []
            return
        try:
            self.edge_flags = [bool(f) for f in flags]
        except (TypeError, ValueError):
            self.edge_flags = []

    def on_pi_status(self, status):
        """Прямой фид разобранного статуса (обычно идёт через sync_*)."""
        if isinstance(status, dict):
            status = dict(status)
            status.setdefault("ts", time.monotonic())
            self.pi_status = status
        else:
            self.pi_status = None

    def on_video_source(self, kind, desc=""):
        self.source_kind = "pi" if str(kind).lower().startswith("pi") else "local"

    def sync_from_status_panel(self, sp):
        """Единый разбор статуса Pi: тянем готовые данные из StatusPanel.

        pi_status — ТОТ ЖЕ нормализованный dict (parse_status_payload),
        source_kind — тот же индикатор источника, roles — тот же фид
        saved_gates.json (push_roles_to_panel в cv_kostyl). local_results
        не трогаем: их кормит unikostyl тем же вызовом, что и панели.
        """
        self.pi_status = sp.pi_status
        self.source_kind = sp.source_kind
        self.roles = dict(getattr(sp, "roles", {}) or {})

    def set_target_gate(self, gid):
        try:
            gid = int(gid)
        except (TypeError, ValueError):
            return
        if 1 <= gid <= 3:
            self.target_gate = gid

    def set_body_zone(self, enabled, frac_r, frac_r2=0.0):
        """Зона корпуса робота (Idea 8 SELF_MASK, доли кадра).

        Диск радара представляет min(w, h)/2 рабочего кадра (rim = край
        кадра), поэтому доля радиуса = frac_r / 0.5. RING-режим исключён
        на стороне unikostyl (там корпус режется «cut center %», геометрия
        полосы другая) — с отключённой маской остаётся круг по умолчанию.
        """
        try:
            frac_r = float(frac_r)
            frac_r2 = float(frac_r2)
        except (TypeError, ValueError):
            frac_r, frac_r2 = 0.0, 0.0
        if enabled and frac_r > 0.0:
            new = _clamp(frac_r / 0.5, 0.06, 0.8)
            new2 = _clamp(frac_r2 / 0.5, 0.0, 0.9) if frac_r2 > frac_r else 0.0
        else:
            new, new2 = DEFAULT_BODY_FRAC, 0.0
        if new != self.body_frac or new2 != self.body_frac2:
            self.body_frac = new
            self.body_frac2 = new2  # _bg_key поменяется -> кэш перестроится

    def set_frame_size(self, w, h):
        """Размер рабочего кадра трекинга (для доли r_px на радиусе)."""
        try:
            m = min(max(1, int(w)), max(1, int(h)))
        except (TypeError, ValueError):
            return
        if m != self._frame_min:
            self._frame_min = m

    def process_mousedown(self, event):
        # Панель только показывает картинку — клики проходят насквозь.
        return False

    # ---------- выбор источника (зеркало status_panel) ----------
    def _use_pi(self):
        if self.source_kind != "pi" or not isinstance(self.pi_status, dict):
            return False
        try:
            age = time.monotonic() - float(self.pi_status.get("ts", 0.0))
        except (TypeError, ValueError):
            return False
        return age <= PI_STATUS_STALE_S

    # ---------- сборка данных ворот ----------
    def _gate_data(self):
        """[{gid, color, configured, found, angle, r_px, dist_cm, dist_ok,
             held, estimated, role}, ...] для ворот 1..3 (углы — как
        пришли, без пересчёта; только санитарная нормализация % 360)."""
        use_pi = self._use_pi()
        gates_src = (
            self.pi_status.get("gates") if use_pi and self.pi_status else None
        )
        out = []
        for i in range(3):
            gid = i + 1
            color = self.gate_colors[i] if i < len(self.gate_colors) else (128, 128, 128)
            rec = {
                "gid": gid, "color": color, "configured": False,
                "found": False, "angle": None, "r_px": None,
                "dist_cm": None, "dist_ok": True, "held": False,
                "estimated": False, "role": None,
            }
            if use_pi:
                g = gates_src.get(str(gid)) if isinstance(gates_src, dict) else None
                if isinstance(g, dict):
                    found = bool(g.get("found"))
                    rec["configured"] = bool(g.get("enabled", False))
                    rec["found"] = found
                    if found:
                        ang = _safe_float(g.get("angle"))
                        if ang is not None:
                            rec["angle"] = ang % 360.0
                        r_px = _safe_float(g.get("r_px"))  # будущее поле (проброс)
                        if r_px is not None and math.isfinite(r_px) and r_px >= 0:
                            rec["r_px"] = r_px
                    dist = _safe_float(g.get("dist_cm"))  # Idea 6 (опционально)
                    if dist is not None and dist > 0:
                        rec["dist_cm"] = dist
                        rec["dist_ok"] = bool(g.get("dist_reliable", True))
                    rec["held"] = bool(g.get("held", False))
                    rec["role"] = _norm_role(g.get("role"))  # нет ключа = None
            else:
                rec_local = None
                if i < len(self.local_results):
                    cand = self.local_results[i]
                    if isinstance(cand, dict) and cand.get("slot") == i:
                        rec_local = cand
                if rec_local is None:
                    for cand in self.local_results:
                        if isinstance(cand, dict) and cand.get("slot") == i:
                            rec_local = cand
                            break
                if isinstance(rec_local, dict):
                    rec["configured"] = bool(rec_local.get("enabled", False))
                    rec["found"] = bool(rec_local.get("found"))
                    if rec["found"]:
                        ang = _safe_float(rec_local.get("angle"))
                        if ang is not None and ang >= 0:
                            rec["angle"] = ang % 360.0
                        r_px = _safe_float(rec_local.get("r_px"))
                        if r_px is not None and math.isfinite(r_px) and r_px >= 0:
                            rec["r_px"] = r_px
                    dist = rec_local.get("dist_cm")
                    if isinstance(dist, (int, float)) and dist > 0:
                        rec["dist_cm"] = float(dist)
                        # Idea 6 добавит dist_reliable в локальный rec —
                        # читаем сразу, чтобы подхват был автоматическим
                        # (нет ключа = достоверная, как в статус-панели).
                        rec["dist_ok"] = bool(rec_local.get("dist_reliable", True))
                    rec["held"] = bool(rec_local.get("held", False))
                rec["role"] = _norm_role(self.roles.get(gid))
            if not rec["configured"]:
                # Отключённый/ненастроенный слот не должен показывать
                # stale/saved значения на радаре.
                rec["found"] = False
                rec["angle"] = None
                rec["r_px"] = None
                rec["dist_cm"] = None
                rec["dist_ok"] = True
                rec["held"] = False
                rec["estimated"] = False
            # --- Idea 10: ворота вне вида + есть привязка поля -> "last
            # known" направление (est). Живое измерение всегда сильнее:
            # запись применяется только когда found=False, а слот настроен.
            est = self.estimated.get(gid)
            if est is not None and not rec["found"] and rec["configured"]:
                ang = _safe_float(est.get("angle_deg"))
                if ang is not None:
                    rec["angle"] = ang % 360.0
                    rec["estimated"] = True
                    d = _safe_float(est.get("dist_cm"))
                    if d is not None and d > 0:
                        rec["dist_cm"] = d
            out.append(rec)
        return out

    # ---------- подпись цели (чистая функция — тестируется напрямую) ----------
    def _target_line(self, gates):
        gid = self.target_gate if 1 <= self.target_gate <= 3 else 1
        g = gates[gid - 1] if 0 <= gid - 1 < len(gates) else None
        if g is None:
            return f"G{gid}: n/a"
        if not g["configured"]:
            return f"G{gid}: not configured"
        if g["angle"] is None or (not g["found"] and not g.get("estimated")):
            # Idea 10: ворота вне вида, но есть привязка поля (estimated) —
            # показываем «last known» направление, а не «not found».
            return f"G{gid}: not found"
        parts = [f"G{gid}", f"{g['angle']:.0f}\u00b0"]
        if g["r_px"] is not None:
            parts.append(f"r = {g['r_px']:.0f} px")
        else:
            # Режим Pi: радиальной дистанции в статусе нет (Idea 6 несёт
            # dist_cm — подхватится автоматически, см. ниже).
            parts.append("r = ? px")
        if g["dist_cm"] is not None and g["dist_ok"]:
            # ASCII "->": U+2192 НЕТ в bundled-шрифте (freesansbold) —
            # рисуется тофу-квадратом (проверено out/radar_check.py).
            parts.append(f"-> {g['dist_cm']:.0f} cm")
        else:
            # До Идеи 6 (или dist_reliable: false — как в статус-панели);
            # U+2014 в шрифте есть, проверено метриками.
            parts.append("-> \u2014")
        if g.get("estimated"):
            # Idea 10: направление из привязки (ворота вне вида) — честная
            # пометка «приблизительно», дуга рисуется приглушённым цветом.
            parts.append("(est)")
        return "  ".join(parts)

    # ---------- кэш текста ----------
    def _text(self, txt, font, color, pos=None, center=None):
        key = (id(font), color, str(txt))
        surf = self._text_cache.get(key)
        if surf is None:
            if len(self._text_cache) > 160:
                self._text_cache.clear()
            surf = font.render(str(txt), True, color)
            self._text_cache[key] = surf
        if center is not None:
            pos = (center[0] - surf.get_width() // 2,
                   center[1] - surf.get_height() // 2)
        self.screen.blit(surf, pos)

    # ---------- статика (перестраивается только при смене размера/зоны) ----------
    def _build_bg(self):
        w, h = self.rect.width, self.rect.height
        surf = pygame.Surface((w, h)).convert()
        surf.fill(PANEL_BG)
        pygame.draw.rect(surf, PANEL_BORDER, surf.get_rect(), 1)

        header_h = self.font_small.get_height() + 5
        footer_h = self.font_small.get_height() + 5
        avail = pygame.Rect(3, header_h, w - 6, max(0, h - header_h - footer_h))
        d = min(avail.w, avail.h)
        cx = avail.x + avail.w // 2
        cy = avail.y + avail.h // 2
        R = max(0, d // 2 - 2)

        if R >= 14:
            # Чёрный диск + тонкий обод.
            pygame.draw.circle(surf, DISC_BG, (cx, cy), R)
            pygame.draw.circle(surf, RIM, (cx, cy), R, 1)
            # Тики обода каждые 30° + румбы (статика).
            for deg in range(0, 360, 30):
                a = math.radians(deg)
                if deg % 90 == 0:
                    r1, r2 = R - 6, R - 1
                else:
                    r1, r2 = R - 3, R - 1
                x1 = cx + r1 * math.cos(a)
                y1 = cy - r1 * math.sin(a)
                x2 = cx + r2 * math.cos(a)
                y2 = cy - r2 * math.sin(a)
                pygame.draw.line(surf, TICK, (x1, y1), (x2, y2), 1)
            if R >= 48:
                # Румбы (статика — рисуем прямо в кэш-поверхность).
                for deg, txt in ((0, "0"), (90, "90"), (180, "180"), (270, "270")):
                    a = math.radians(deg)
                    lr = R - 13
                    lbl = self.font_small.render(txt, True, TICK)
                    surf.blit(lbl, (
                        cx + lr * math.cos(a) - lbl.get_width() // 2,
                        cy - lr * math.sin(a) - lbl.get_height() // 2,
                    ))
            # Зона корпуса робота (Idea 8 self-маска): светлый круг в центре.
            br = int(round(self.body_frac * R))
            if br >= 3:
                pygame.draw.circle(surf, BODY_ZONE, (cx, cy), br)
                pygame.draw.circle(surf, BODY_BORDER, (cx, cy), br, 1)
            if self.body_frac2 > self.body_frac + 0.02:
                r2 = int(round(self.body_frac2 * R))
                if r2 < R - 1:
                    pygame.draw.circle(surf, BODY_RING, (cx, cy), r2, 1)

        self._bg = surf
        self._geo = (cx, cy, R, h - footer_h + 2)
        self._bg_key = (w, h, self.body_frac, self.body_frac2)

    # ---------- отрисовка ----------
    def draw(self):
        if not self.visible:
            return  # выключен кнопкой — цена ровно ноль
        r = self.rect
        if r.width < 60 or r.height < 60:
            return
        key = (r.width, r.height, self.body_frac, self.body_frac2)
        if self._bg is None or self._bg_key != key:
            self._build_bg()
        self.screen.blit(self._bg, r.topleft)
        cx, cy, R, footer_y = self._geo
        # _geo в координатах ПАНЕЛИ (кэш-поверхность) -> на экран с оффсетом.
        cx += r.x
        cy += r.y
        footer_y += r.y
        if R < 14:
            return

        use_pi = self._use_pi()
        gates = self._gate_data()

        # Шапка: RADAR + тег источника данных (как тег "pi"/"local" панели).
        self._text("RADAR", self.font_small, HEADER_TXT, pos=(r.x + 6, r.y + 3))
        if use_pi:
            tag, tag_color = "pi", TAG_PI
        elif self.source_kind == "pi":
            tag, tag_color = "stale", TAG_STALE
        else:
            tag, tag_color = "local", TAG_LOCAL
        self._text(tag, self.font_small, tag_color,
                   center=(r.right - 14, r.y + 3 + self.font_small.get_height() // 2))

        # Дуги ворот (найденные; угол пришёл из трекинга — не пересчитываем).
        Rg = R - ARC_RADIUS_INSET
        arc_w = max(3, R // 14)
        half = math.radians(ARC_SPAN_DEG)
        role_half = math.radians(ROLE_SPAN_DEG)
        arc_rect = pygame.Rect(0, 0, 2 * Rg, 2 * Rg)
        role_rect = pygame.Rect(0, 0, 2 * (R - ROLE_RADIUS_INSET),
                                2 * (R - ROLE_RADIUS_INSET))

        # Idea 11: опасные сектора границы (советчик) — красные дуги у обода
        # ПОД дугами ворот + тег EDGE в шапке. Флагов нет -> ни одной
        # операции (нулевой цена; default-состояние всегда без флагов).
        if any(self.edge_flags):
            n = len(self.edge_flags)
            step = 2.0 * math.pi / max(n, 1)
            gap = 0.06 * step  # щель между секторами (читаемость)
            edge_rect = pygame.Rect(0, 0, 2 * Rg, 2 * Rg)
            for k, flag in enumerate(self.edge_flags):
                if not flag:
                    continue
                edge_rect.center = (cx, cy)
                pygame.draw.arc(
                    self.screen, EDGE_ARC, edge_rect,
                    k * step + gap, (k + 1) * step - gap, arc_w,
                )
            self._text("EDGE", self.font_small, TAG_STALE,
                       center=(r.centerx,
                               r.y + 3 + self.font_small.get_height() // 2))

        for g in gates:
            if not g["configured"] or g["angle"] is None:
                continue  # не найден / не настроен — позиции нет
            a = math.radians(g["angle"])
            # Приглушённый цвет: held (гистерезис Идеи 5) ИЛИ estimated
            # («last known» из привязки поля, Идея 10) — оба не живые
            # измерения, честно отличаются от ярких дуг.
            dimmed = g["held"] or g["estimated"]
            color = self._dim_colors[g["gid"] - 1] if (
                dimmed and g["gid"] - 1 < len(self._dim_colors)
            ) else g["color"]
            arc_rect.center = (cx, cy)
            pygame.draw.arc(self.screen, color, arc_rect, a - half, a + half, arc_w)
            # Idea 9: бейдж роли — тонкая дужка под дугой ворот (green/red);
            # роль не задана («unset» == отсутствие ключа) — бейджа нет.
            if g["role"]:
                role_color = ROLE_OURS if g["role"] == "ours" else ROLE_OPPONENT
                role_rect.center = (cx, cy)
                pygame.draw.arc(
                    self.screen, role_color, role_rect,
                    a - role_half, a + role_half, 2,
                )

        # Цель (TARGET G1..G3): круг-маркер + линия от центра + подписи.
        max_r_px = self._frame_min / 2.0 if self._frame_min > 0 else 0.0
        tgt = gates[self.target_gate - 1] if 1 <= self.target_gate <= 3 else None
        if tgt is not None and tgt["found"] and tgt["angle"] is not None:
            a = math.radians(tgt["angle"])
            mr_frac = self._marker_frac(tgt, max_r_px)
            mr = mr_frac * R
            mx = cx + mr * math.cos(a)
            my = cy - mr * math.sin(a)
            pygame.draw.line(self.screen, MARKER_TXT, (cx, cy), (mx, my), 1)
            marker_r = marker_radius(R)
            pygame.draw.circle(self.screen, MARKER_TXT, (int(mx), int(my)),
                               marker_r, 2)
            # Подпись угла «52°» — внутри маркера, к центру (не залезает на
            # дуги). Цвет по фону: белая на чёрном диске, тёмная над светлой
            # зоной корпуса (иначе белое на светло-сером не читается).
            lr = max(0.0, mr - marker_r - 12)
            lbl_color = (MARKER_TXT if lr >= self.body_frac * R
                         else MARKER_TXT_ON_BODY)
            self._text(f"{tgt['angle']:.0f}\u00b0", self.font_marker, lbl_color,
                       center=(cx + lr * math.cos(a), cy - lr * math.sin(a)))

        # Нижняя строка: G<n> <angle>° r = N px → M cm (см. _target_line).
        self._text(self._target_line(gates), self.font_small, FOOTER_TXT,
                   pos=(r.x + 6, footer_y))

    def _marker_frac(self, tgt, max_r_px):
        """Доля радиуса для маркера цели: dist_cm (Idea 6) > r_px > фикс.

        Грубая визуальная подсказка, не измерение: точные числа всегда в
        подписи нижней строки."""
        dist = tgt["dist_cm"] if tgt["dist_ok"] else None
        if dist is not None and dist > 0:
            return _clamp(dist / RADAR_MAX_CM, 0.30, 0.92)
        r_px = tgt["r_px"]
        if r_px is not None and max_r_px > 0:
            return _clamp(r_px / max_r_px, 0.12, 0.92)
        return DEFAULT_MARKER_FRAC

# ================= Идея 7 (запрос пользователя): отдельное окно RADAR ========
# RADAR открывается в собственном cv2-окне (WINDOW_NORMAL — свободный ресайз
# мышью), рисует ТОТ ЖЕ стейт, что и панель в UI (единый источник —
# _gate_data(), без дублирования парсинга). Кнопка RADAR переключает окно;
# панель в UI остаётся скрытой по умолчанию. Дёшево: отрисовка только пока
# окно открыто. ESC в окне закрывает радар.
import cv2 as _cv2
import numpy as _np

_RADAR_WIN = "RADAR"
_RADAR_WIN_SIZE = 440

def _radar_bgr(rgb):
    return (int(rgb[2]), int(rgb[1]), int(rgb[0]))

def _radar_arc(img, center, radius, angle_deg, span_deg, color_bgr, thickness):
    _cv2.ellipse(img, center, (radius, radius), 0.0,
                 angle_deg - span_deg / 2.0, angle_deg + span_deg / 2.0,
                 color_bgr, thickness, _cv2.LINE_AA)

def _radar_toggle_window(self):
    if getattr(self, "_win_open", False):
        try:
            _cv2.destroyWindow(_RADAR_WIN)
        except _cv2.error:
            pass
        self._win_open = False
        return
    _cv2.namedWindow(_RADAR_WIN, _cv2.WINDOW_NORMAL)
    _cv2.resizeWindow(_RADAR_WIN, _RADAR_WIN_SIZE, _RADAR_WIN_SIZE)
    self._win_open = True

def _radar_close_window(self):
    if getattr(self, "_win_open", False):
        try:
            _cv2.destroyWindow(_RADAR_WIN)
        except _cv2.error:
            pass
    self._win_open = False

def _radar_render_window_bgr(self, size):
    img = _np.full((size, size, 3), (38, 38, 38), dtype=_np.uint8)
    cx = cy = size // 2
    R = size // 2 - 14
    _cv2.circle(img, (cx, cy), R, (5, 5, 5), -1)
    _cv2.circle(img, (cx, cy), R, (70, 70, 70), 2)
    for k in range(12):  # риски каждые 30°
        a = math.radians(k * 30.0)
        p1 = (int(cx + (R - 2) * math.cos(a)), int(cy + (R - 2) * math.sin(a)))
        p2 = (int(cx + (R - 10) * math.cos(a)), int(cy + (R - 10) * math.sin(a)))
        _cv2.line(img, p1, p2, (110, 110, 110), 1, _cv2.LINE_AA)
    # тело робота — зона self-маски (Идея 8)
    _cv2.circle(img, (cx, cy), max(2, int(self.body_frac * R)), (225, 225, 225), -1)
    if self.body_frac2 > self.body_frac:
        _cv2.circle(img, (cx, cy), max(3, int(self.body_frac2 * R)),
                    (150, 150, 150), 1)
    # EDGE-сектора (Идея 11): красные дуги у обода
    if any(self.edge_flags):
        n = len(self.edge_flags)
        step = 360.0 / max(n, 1)
        for k, flag in enumerate(self.edge_flags):
            if flag:
                _radar_arc(img, (cx, cy), R + 5, k * step + step / 2.0,
                           step * 0.72, (45, 45, 230), max(2, size // 90))
    # ворота — единый источник стейта _gate_data()
    for g in self._gate_data():
        if not g["configured"] or g["angle"] is None:
            continue
        col = _radar_bgr(g["color"])
        if g["held"] or g["estimated"]:
            col = (col[0] // 3, col[1] // 3, col[2] // 3)
        if not g["found"]:
            col = (col[0] // 2, col[1] // 2, col[2] // 2)
        _radar_arc(img, (cx, cy), R, g["angle"], ARC_SPAN_DEG * 2.0, col,
                   max(3, size // 60))
        if g["role"]:
            rc = (80, 220, 90) if g["role"] == "ours" else (70, 70, 230)
            _radar_arc(img, (cx, cy), max(6, R - 12), g["angle"],
                       ROLE_SPAN_DEG * 2.0, rc, max(2, size // 100))
        if g["gid"] == self.target_gate:
            if g["dist_cm"]:
                frac = _clamp(g["dist_cm"] / RADAR_MAX_CM, 0.30, 0.92)
            else:
                frac = DEFAULT_MARKER_FRAC
            mx = int(cx + frac * R * math.cos(math.radians(g["angle"])))
            my = int(cy + frac * R * math.sin(math.radians(g["angle"])))
            _cv2.line(img, (cx, cy), (mx, my), (255, 255, 255), 1, _cv2.LINE_AA)
            _cv2.circle(img, (mx, my), max(4, size // 55), (255, 255, 255), 2,
                        _cv2.LINE_AA)
            label = f"G{g['gid']} {g['angle']:.0f} deg"
            if g["dist_cm"]:
                label += (f" -> {g['dist_cm']:.0f} cm" if g["dist_ok"]
                          else " -> d?")
            _cv2.putText(img, label, (6, size - 10),
                         _cv2.FONT_HERSHEY_SIMPLEX,
                         max(0.45, size / 800.0), (235, 235, 235), 1,
                         _cv2.LINE_AA)
    return img

def _radar_pump_window(self, do_waitkey=True):
    if not getattr(self, "_win_open", False):
        return
    _cv2.imshow(_RADAR_WIN, self._render_window_bgr(_RADAR_WIN_SIZE))
    if do_waitkey:
        k = _cv2.waitKey(1)
        if k == 27:  # ESC закрывает радар
            self.close_window()
            return
        try:
            # Крестик в title bar: WINDOW_NORMAL больше не виден.
            if _cv2.getWindowProperty(_RADAR_WIN, _cv2.WND_PROP_VISIBLE) < 0.5:
                self._win_open = False  # уже скрыто cv2 — destroy не нужен
        except _cv2.error:
            self._win_open = False

RadarPanel.toggle_window = _radar_toggle_window
RadarPanel.close_window = _radar_close_window
RadarPanel.pump_window = _radar_pump_window
RadarPanel._render_window_bgr = _radar_render_window_bgr
