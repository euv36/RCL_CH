# -*- coding: utf-8 -*-
"""status_panel.py — Idea 2: панель статуса ворот + разбор multipart-статуса Pi.

Две независимые части:

1. `parse_status_payload()` — разбор части 2 multipart-сообщения видео 5555.
   Протокол (additive-расширение Idea 2, одиночный JPEG остаётся рабочим):
       Pi -> ноутбук: ZMQ multipart [JPEG bytes, JSON bytes] на tcp://*:5555
   JSON (поля `dist_*` добавляет Idea 6, `role` — Idea 9; они опциональны и
   просто пробрасываются дальше, если есть):
       {"gates": {"1": {"enabled": true, "found": true,
                        "angle": 123, "area": 456,
                        "dist_cm": 148.0, "dist_reliable": true},
                  "2": {...}, "3": {...}},
        "fps": 25.9}
   Ключи ворот — строки "1".."3". `found: false` -> ключей angle/area может не
   быть. Ворота, которых нет у Pi (--gates 2), в словаре отсутствуют.

2. `StatusPanel` — pygame-виджет на примитивах `widgets.py` (Widget, стиль
   LogPanel): свотчи цвета ворот, настроен/не настроен, найден/потерян (live),
   угол/площадь, FPS обработки, источник видео (локальная камера / Pi-стрим),
   статус публикации цветов 5557. Idea 9: бейдж роли у строки ворот —
   «НАШИ» (зелёный) / «ПРОТИВНИК» (красный); в режиме Pi роль берётся из
   статус-JSON (поле "role", Idea 9), в локальном — из set_roles()
   (cv_kostyl feeds it from saved_gates.json). Ничего не рисует на кадре —
   рисует только себя на экране UI.

Живость ("live"): статус Pi старше PI_STATUS_STALE_S секунд считается
устаревшим — панель честно возвращается к локальному трекингу ноутбука и
помечает источник как "stale". Битый/чужой JSON не роняет UI: парсер
возвращает None, панель держит последний хороший статус до истечения срока.

Ничего из этого модуля не знает про unikostyl/cv_kostyl — только обратные
колбэки, поэтому его можно тестировать без камеры и без pygame-экрана.
"""

import json
import time

import pygame

from widgets import Widget

# Статус Pi старше этого (сек) — ссылка потеряна/старый Pi без multipart.
PI_STATUS_STALE_S = 2.0
# Сколько секунд показывать "N s" с момента последней отправки цветов в 5557.
PUB_SHOW_SENT_FOR_S = 60.0

# --- визуальный стиль: тёмная панель, единый вид главного окна ---
PANEL_BG = (31, 38, 54)
PANEL_BORDER = (80, 96, 128)
TXT = (232, 238, 247)
TXT_DIM = (155, 166, 190)
OK_GREEN = (72, 210, 135)
BAD_RED = (255, 110, 96)
STATE_FOUND = (72, 210, 135)
STATE_LOST = (255, 126, 96)
STATE_OFF = (155, 166, 190)
SWATCH_BORDER = (120, 132, 160)

# Idea 9: бейджи роли ворот (зелёный «наши» / красный «противник»).
ROLE_OURS = (0, 128, 64)
ROLE_OPPONENT = (188, 40, 35)
ROLE_BADGE_BG = (47, 78, 62)
ROLE_BADGE_TXT = {"ours": "НАШИ", "opponent": "ПРОТИВНИК"}


def _norm_role(value):
    """Роль статуса/файла -> "ours"|"opponent"|None (None = нет бейджа)."""
    return value if value in ("ours", "opponent") else None


def parse_status_payload(payload):
    """Разбирает JSON-статус Pi (часть 2 multipart-сообщения 5555).

    payload: bytes | str. Возвращает нормализованный dict вида
        {"gates": {str(id): dict, ...}, "fps": float | None,
         "extra": {...прочие ключи верхнего уровня...}, "ts": <monotonic>}
    или None, если данные не похожи на статус (битый JSON, не словарь,
    нет поля gates). В gate-записях `found`/`enabled` приводятся к bool;
    отсутствие `enabled` считается ненастроенным слотом (stale/saved статус
    не должен возрождать ворота), остальные поля сохраняются как есть —
    панель показывает те, что знает.
    """
    ts = time.monotonic()
    try:
        if isinstance(payload, bytes):
            payload = payload.decode("utf-8", "replace")
        data = json.loads(payload)
    except (ValueError, TypeError):
        return None
    if not isinstance(data, dict):
        return None

    raw_gates = data.get("gates")
    if not isinstance(raw_gates, dict):
        # Статус без ворот бесполезен для панели (но не падаем).
        return None

    gates = {}
    for key, val in raw_gates.items():
        if not isinstance(val, dict):
            continue
        entry = {"found": bool(val.get("found", False))}
        if "enabled" in val:
            entry["enabled"] = bool(val["enabled"])
        else:
            # Не выводим stale/saved-ворота без явной настройки слота.
            entry["enabled"] = False
        # angle/area/dist_cm/dist_reliable/role/... — как есть.
        for fld, v in val.items():
            if fld in ("found", "enabled"):
                continue
            entry[fld] = v
        gates[str(key)] = entry

    fps = None
    if isinstance(data.get("fps"), (int, float)):
        fps = float(data["fps"])

    extra = {k: v for k, v in data.items() if k not in ("gates", "fps")}
    return {"gates": gates, "fps": fps, "extra": extra, "ts": ts}


def _safe_float(val):
    try:
        if isinstance(val, bool):
            return None
        f = float(val)
    except (TypeError, ValueError):
        return None
    return f


def _rounded_rect(surface, color, rect, width=0):
    """Прямоугольник со скруглением; на старых pygame — без скругления."""
    try:
        pygame.draw.rect(surface, color, rect, width, border_radius=4)
    except (TypeError, ValueError):
        pygame.draw.rect(surface, color, rect, width)


class StatusPanel(Widget):
    """Панель статуса (Idea 2). Только отображение: клики не перехватывает.

    Данные подаются извне:
      * on_pi_status(status)   — из cv_kostyl.py при разборе multipart 5555;
      * on_video_source(kind, desc) — 'local'|'pi' + индекс/IP, при смене;
      * on_publish_state(ok, note)  — bind 5557 удался/упал;
      * on_colors_sent()       — после каждой отправки цветов в 5557;
      * set_local_results(track_results) — из unikostyl каждый кадр;
      * note_frame(is_real_frame) — такт основного цикла (для FPS UI).

    Таблица ворот берётся из статуса Pi (если источник Pi и статус свежий),
    иначе — из локального трекинга unikostyl; в углу панели указано, что
    именно показано ("pi"/"local").
    """

    def __init__(self, screen, rect, gate_colors):
        super().__init__(screen, rect, block_click=False)
        self.rect = pygame.Rect(rect)
        self.gate_colors = tuple(gate_colors)  # RGB-кортежи, как TRACK_COLORS
        self.font = pygame.font.Font(None, 19)
        self.font_small = pygame.font.Font(None, 16)

        # Данные (заполняются колбэками/сеттерами).
        self.pi_status = None      # последний нормализованный статус Pi
        self.local_results = []    # track_results из unikostyl
        self.roles = {}            # Idea 9: {gate_id(int): "ours"|"opponent"}
        self.source_kind = "local"  # 'local' | 'pi'
        self.source_desc = "0"
        self.pub_ok = None         # None = ещё неизвестно, True/False = bind
        self.pub_note = ""
        self.last_send_t = None    # monotonic последней отправки цветов
        self.ui_fps = 0.0          # FPS приходящих кадров (EMA)
        self._last_frame_t = None

    def set_roles(self, roles):
        """Idea 9: роли для ЛОКАЛЬНОЙ таблицы (источник — saved_gates.json).

        roles: {gate_id: "ours"|"opponent"} — только заданные роли; слоты
        без роли бейджа не получают. В режиме Pi роль приходит в статус-JSON
        (build_gate_status) и читается прямо из записи ворот.
        """
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

    # ---------- колбэки из cv_kostyl.py ----------
    def on_pi_status(self, status):
        """status — результат parse_status_payload() (или None = сброс)."""
        if isinstance(status, dict):
            status = dict(status)
            status.setdefault("ts", time.monotonic())
            self.pi_status = status
        else:
            self.pi_status = None

    def on_video_source(self, kind, desc=""):
        self.source_kind = "pi" if str(kind).lower().startswith("pi") else "local"
        self.source_desc = str(desc)

    def on_publish_state(self, ok, note=""):
        self.pub_ok = bool(ok)
        self.pub_note = str(note)

    def on_colors_sent(self):
        self.last_send_t = time.monotonic()

    # ---------- колбэки из unikostyl.main_loop_frame ----------
    def set_local_results(self, track_results):
        self.local_results = list(track_results or [])

    def note_frame(self, is_real_frame):
        now = time.monotonic()
        if is_real_frame:
            if self._last_frame_t is not None:
                dt = now - self._last_frame_t
                if 0.0 < dt < 1.0:
                    f = 1.0 / dt
                    self.ui_fps = f if self.ui_fps <= 0.0 else (
                        self.ui_fps * 0.85 + f * 0.15
                    )
            self._last_frame_t = now

    def process_mousedown(self, event):
        # Панель только показывает состояние — клики сквозь неё.
        return False

    # ---------- отрисовка ----------
    def draw(self):
        r = self.rect
        if r.width < 60 or r.height < 50:
            return
        _rounded_rect(self.screen, PANEL_BG, r)
        _rounded_rect(self.screen, PANEL_BORDER, r, width=1)

        now = time.monotonic()
        pi_age = None
        if self.pi_status is not None:
            pi_age = now - float(self.pi_status.get("ts", 0.0))
        pi_fresh = pi_age is not None and pi_age <= PI_STATUS_STALE_S
        use_pi = self.source_kind == "pi" and pi_fresh

        y = r.y + 5
        # Заголовок + метка источника данных таблицы ворот.
        tag = "pi" if use_pi else "local"
        tag_color = OK_GREEN if use_pi else TXT_DIM
        self._text(r.x + 7, y, "GATES", self.font_small, TXT_DIM)
        surf = self.font_small.render(tag, True, tag_color)
        self.screen.blit(surf, (r.right - 8 - surf.get_width(), y))
        y += self.font_small.get_height() + 3

        if use_pi:
            self._draw_gates_pi(r, y, now)
        else:
            self._draw_gates_local(r, y)
        y += 3 * (self.font.get_height() + 2) + 4

        # Разделитель.
        pygame.draw.line(
            self.screen, (190, 190, 190), (r.x + 6, y), (r.right - 6, y), 1
        )
        y += 4

        # Источник видео.
        if self.source_kind == "pi":
            if pi_fresh:
                src_txt = f"SRC: Pi {self.source_desc} (live)"
                src_color = OK_GREEN
            elif self.pi_status is not None:
                src_txt = f"SRC: Pi {self.source_desc} (stale)"
                src_color = BAD_RED
            else:
                src_txt = f"SRC: Pi {self.source_desc} (no status)"
                src_color = BAD_RED
        else:
            src_txt = f"SRC: local cam {self.source_desc}"
            src_color = TXT
        self._text(r.x + 7, y, src_txt, self.font_small, src_color)
        y += self.font_small.get_height() + 2

        # Публикация цветов 5557.
        if self.pub_ok is None:
            pub_txt = "PUB 5557: ..."
            pub_color = TXT_DIM
        elif self.pub_ok:
            pub_txt = "PUB 5557: bound"
            pub_color = OK_GREEN
            if self.last_send_t is not None:
                age = now - self.last_send_t
                if age <= PUB_SHOW_SENT_FOR_S:
                    pub_txt += f", sent {age:.0f}s"
        else:
            pub_txt = f"PUB 5557: ERR {self.pub_note[:14]}"
            pub_color = BAD_RED
        self._text(r.x + 7, y, pub_txt, self.font_small, pub_color)
        y += self.font_small.get_height() + 2

        # FPS обработки: UI (кадры/с в main_loop_frame) и Pi (из статуса).
        fps_parts = [f"FPS ui {self.ui_fps:.1f}"]
        if use_pi:
            pi_fps = self.pi_status.get("fps")
            if pi_fps is not None:
                fps_parts.append(f"pi {pi_fps:.1f}")
        self._text(r.x + 7, y, " | ".join(fps_parts), self.font_small, TXT)

    # ---------- строки ворот ----------
    def _draw_gates_pi(self, r, y, now):
        gates = self.pi_status.get("gates", {})
        for i in range(3):
            gate_id = str(i + 1)
            g = gates.get(gate_id)
            # Idea 9: роль приходит в статус-JSON только когда задана.
            role = _norm_role(g.get("role")) if isinstance(g, dict) else None
            if g is None:
                self._gate_row(
                    r.x + 7, y, i, "—", TXT_DIM, state_txt="n/a",
                    state_color=TXT_DIM, role=role, right=r.right,
                )
            elif g.get("found"):
                bits = []
                angle = _safe_float(g.get("angle"))
                area = _safe_float(g.get("area"))
                if angle is not None:
                    bits.append(f"{angle:.0f} deg")
                if area is not None:
                    bits.append(f"{area:.0f} px")
                dist = _safe_float(g.get("dist_cm"))
                if dist is not None and g.get("dist_reliable", True):
                    bits.append(f"{dist:.0f} cm")
                self._gate_row(
                    r.x + 7, y, i, " ".join(bits) or "found",
                    TXT, state_txt="FOUND", state_color=STATE_FOUND,
                    role=role, right=r.right,
                )
            elif g.get("enabled"):
                self._gate_row(
                    r.x + 7, y, i, "", TXT, state_txt="LOST",
                    state_color=STATE_LOST, role=role, right=r.right,
                )
            else:
                self._gate_row(
                    r.x + 7, y, i, "", TXT_DIM, state_txt="OFF",
                    state_color=STATE_OFF, role=role, right=r.right,
                )
            y += self.font.get_height() + 2

    def _draw_gates_local(self, r, y):
        for i in range(3):
            rec = self.local_results[i] if i < len(self.local_results) else None
            # Idea 9: локальные роли — из saved_gates.json (set_roles()).
            role = self.roles.get(i + 1)
            if rec is None:
                self._gate_row(
                    r.x + 7, y, i, "—", TXT_DIM, state_txt="n/a",
                    state_color=TXT_DIM, role=role, right=r.right,
                )
            elif not rec.get("enabled", True):
                self._gate_row(
                    r.x + 7, y, i, "", TXT_DIM, state_txt="OFF",
                    state_color=STATE_OFF, role=role, right=r.right,
                )
            elif rec.get("found"):
                bits = [f"{int(rec.get('angle', -1))} deg"]
                area = _safe_float(rec.get("area"))
                if area is not None:
                    bits.append(f"{area:.0f} px")
                self._gate_row(
                    r.x + 7, y, i, " ".join(bits), TXT,
                    state_txt="FOUND", state_color=STATE_FOUND,
                    role=role, right=r.right,
                )
            else:
                self._gate_row(
                    r.x + 7, y, i, "", TXT, state_txt="LOST",
                    state_color=STATE_LOST, role=role, right=r.right,
                )
            y += self.font.get_height() + 2

    def _gate_row(self, x, y, idx, value_txt, value_color, state_txt,
                  state_color, role=None, right=None):
        """Свотч цвета ворот + G<id> + состояние + угол/площадь.

        Idea 9: role="ours"/"opponent" -> бейдж «НАШИ»/«ПРОТИВНИК» у правого
        края строки; значение обрезается, чтобы не залезать под бейдж.
        """
        # Свотч: квадрат цвета ворот (как на кнопках/маске TRACK_COLORS).
        sw_rect = pygame.Rect(x, y + 2, 11, 11)
        color = self.gate_colors[idx] if idx < len(self.gate_colors) else (128, 128, 128)
        pygame.draw.rect(self.screen, color, sw_rect)
        pygame.draw.rect(self.screen, SWATCH_BORDER, sw_rect, 1)

        x += sw_rect.width + 5
        self._text(x, y, f"G{idx + 1}", self.font, TXT)
        x += self.font.size("G9")[0] + 5
        self._text(x, y, state_txt, self.font, state_color)
        x += self.font.size("FOUND ")[0] + 4

        # --- Idea 9: бейдж роли (правый край строки) ---
        badge_rect = None
        badge_txt = ROLE_BADGE_TXT.get(role) if role else None
        if badge_txt and right is not None:
            badge_w = self.font_small.size(badge_txt)[0] + 8
            bx = right - 7 - badge_w
            badge_rect = pygame.Rect(
                bx, y + 1, badge_w, self.font_small.get_height() + 3)

        if value_txt:
            # Клип значения по левому краю бейджа (или до правого края панели).
            limit = (badge_rect.x - 5) if badge_rect is not None else (
                (right - 7) if right is not None else 10 ** 6)
            avail = limit - x
            if self.font.size(value_txt)[0] > avail:
                value_txt = self._fit_text(value_txt, self.font, avail)
            if value_txt:
                self._text(x, y, value_txt, self.font, value_color)

        if badge_rect is not None:
            badge_color = ROLE_OURS if role == "ours" else ROLE_OPPONENT
            _rounded_rect(self.screen, badge_color, badge_rect)
            surf = self.font_small.render(badge_txt, True, (255, 255, 255))
            self.screen.blit(
                surf,
                (badge_rect.x + (badge_rect.w - surf.get_width()) // 2,
                 badge_rect.y + (badge_rect.h - surf.get_height()) // 2),
            )

    @staticmethod
    def _fit_text(txt, font, max_w):
        """Обрезает текст под max_w пикселей (хвост -> ".."), None если 0."""
        if max_w <= 0:
            return ""
        if font.size(txt)[0] <= max_w:
            return txt
        while txt and font.size(txt + "..")[0] > max_w:
            txt = txt[:-1]
        return (txt + "..") if txt else ""

    def _text(self, x, y, txt, font, color):
        surf = font.render(str(txt), True, color)
        self.screen.blit(surf, (x, y))
