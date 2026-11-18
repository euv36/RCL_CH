# -*- coding: utf-8 -*-
"""Modal Russian INFO overlay for the pygame UI."""
import pygame

import help_content

_SECTION_IDS = [s["id"] for s in help_content.SECTIONS]
_SECTION_TITLES = {s["id"]: s["title"] for s in help_content.SECTIONS}
_SECTIONS = {s["id"]: s["lines"] for s in help_content.SECTIONS}


def _font(size, bold=False):
    for name in ("arial", "segoe ui", "tahoma", "calibri"):
        try:
            f = pygame.font.SysFont(name, size, bold=bold)
            if f:
                return f
        except Exception:
            pass
    return pygame.font.Font(None, size)


class HelpOverlay:
    def __init__(self, screen_size=(1280, 720)):
        self.screen_size = tuple(screen_size)
        self.visible = False
        self.section_id = "main"
        self.scroll = 0
        self._wrapped = {}
        self._max_scroll = 0

    def toggle(self, section_id=None):
        if section_id in _SECTIONS:
            self.section_id = section_id
            self.visible = True
            self.scroll = 0
            return
        self.visible = not self.visible
        self.scroll = 0

    def set_screen_size(self, size):
        self.screen_size = tuple(size)
        self._ensure_metrics()
        self.scroll = min(self.scroll, self._max_scroll)

    def _ensure_metrics(self):
        w, h = self.screen_size
        margin = 24
        panel = pygame.Rect(margin, margin, w - 2 * margin, h - 2 * margin)
        content = self._content_rect(panel)
        font_body = _font(17)
        lines = self._wrapped_lines(self.section_id, content.width - 28, font_body)
        visible = self._visible_line_count(content, font_body)
        self._max_scroll = max(0, len(lines) - visible)
        self.scroll = max(0, min(self.scroll, self._max_scroll))

    def tab_rects(self):
        w, h = self.screen_size
        margin = 24
        panel_w = w - 2 * margin
        left_w = 235
        top = margin + 58
        row_h = 42
        gap = 8
        rects = {}
        for i, sid in enumerate(_SECTION_IDS):
            rects[sid] = pygame.Rect(margin + 16, top + i * (row_h + gap), left_w - 32, row_h)
        return rects

    def close_rect(self):
        w, h = self.screen_size
        margin = 24
        return pygame.Rect(w - margin - 52, margin + 14, 34, 34)

    def handle_event(self, event):
        if not self.visible:
            return False
        self._ensure_metrics()

        if event.type == pygame.KEYDOWN:
            key = event.key
            if key == pygame.K_ESCAPE:
                self.visible = False
                return True
            if key in (pygame.K_DOWN,):
                self.scroll = min(self.scroll + 1, self._max_scroll)
                return True
            if key in (pygame.K_UP,):
                self.scroll = max(self.scroll - 1, 0)
                return True
            if key in (pygame.K_PAGEDOWN, pygame.K_SPACE):
                self.scroll = min(self.scroll + 8, self._max_scroll)
                return True
            if key in (pygame.K_PAGEUP,):
                self.scroll = max(self.scroll - 8, 0)
                return True
            if key == pygame.K_HOME:
                self.scroll = 0
                return True
            if key == pygame.K_END:
                self.scroll = self._max_scroll
                return True
            return True

        if event.type == pygame.MOUSEWHEEL:
            self.scroll = max(0, min(self._max_scroll, self.scroll - int(event.y)))
            return True

        if event.type == pygame.MOUSEBUTTONDOWN:
            pos = getattr(event, "pos", None)
            if pos is None:
                return True
            if self.close_rect().collidepoint(pos):
                self.visible = False
                return True
            for sid, rect in self.tab_rects().items():
                if rect.collidepoint(pos):
                    self.section_id = sid
                    self.scroll = 0
                    return True
            return True

        if event.type in (pygame.MOUSEBUTTONUP, pygame.MOUSEMOTION):
            return True
        return True

    def draw(self, surface):
        if not self.visible:
            return
        self.screen_size = surface.get_size()
        w, h = surface.get_size()
        margin = 24
        panel = pygame.Rect(margin, margin, w - 2 * margin, h - 2 * margin)

        # Dim the whole app behind the modal panel.
        overlay = pygame.Surface((w, h), pygame.SRCALPHA)
        overlay.fill((8, 10, 16, 190))
        surface.blit(overlay, (0, 0))

        pygame.draw.rect(surface, (30, 34, 44), panel, border_radius=14)
        pygame.draw.rect(surface, (78, 88, 112), panel, 2, border_radius=14)

        font_title = _font(26, bold=True)
        font_tab = _font(19, bold=True)
        font_body = _font(17)
        font_small = _font(14)

        title = font_title.render("INFO — справка по интерфейсу", True, (245, 247, 252))
        surface.blit(title, (panel.left + 22, panel.top + 16))

        close = self.close_rect()
        pygame.draw.rect(surface, (66, 72, 88), close, border_radius=8)
        pygame.draw.rect(surface, (120, 132, 160), close, 1, border_radius=8)
        cx, cy = close.center
        pygame.draw.line(surface, (235, 238, 245), (cx - 7, cy - 7), (cx + 7, cy + 7), 2)
        pygame.draw.line(surface, (235, 238, 245), (cx + 7, cy - 7), (cx - 7, cy + 7), 2)

        # Left column: section tabs.
        tab_rects = self.tab_rects()
        for sid, rect in tab_rects.items():
            active = sid == self.section_id
            bg = (58, 96, 150) if active else (42, 47, 60)
            border = (126, 170, 232) if active else (68, 76, 96)
            pygame.draw.rect(surface, bg, rect, border_radius=10)
            pygame.draw.rect(surface, border, rect, 1, border_radius=10)
            label = _SECTION_TITLES[sid]
            text = font_tab.render(label, True, (245, 248, 255) if active else (205, 210, 222))
            surface.blit(text, (rect.left + 12, rect.centery - text.get_height() // 2))

        # Right column: wrapped, scrolled text.
        content = self._content_rect(panel)
        pygame.draw.rect(surface, (24, 27, 36), content, border_radius=10)
        pygame.draw.rect(surface, (64, 72, 92), content, 1, border_radius=10)
        header = font_tab.render(_SECTION_TITLES[self.section_id], True, (160, 205, 255))
        surface.blit(header, (content.left + 14, content.top + 10))

        lines = self._wrapped_lines(self.section_id, content.width - 28, font_body)
        self._max_scroll = max(0, len(lines) - self._visible_line_count(content, font_body))
        self.scroll = max(0, min(self.scroll, self._max_scroll))
        y = content.top + 46
        count = 0
        visible = self._visible_line_count(content, font_body)
        for line in lines[self.scroll:self.scroll + visible]:
            rendered = font_body.render(line, True, (228, 231, 238))
            surface.blit(rendered, (content.left + 14, y))
            y += font_body.get_linesize() + 2
            count += 1

        footer = font_small.render("ESC / крестик — закрыть · колесо / PageUp / PageDown — прокрутка", True, (150, 158, 178))
        surface.blit(footer, (content.left + 14, content.bottom - 24))

        # Scrollbar.
        if self._max_scroll > 0:
            track = pygame.Rect(content.right - 12, content.top + 42, 6, content.height - 62)
            pygame.draw.rect(surface, (58, 64, 80), track, border_radius=3)
            frac = visible / float(len(lines))
            thumb_h = max(28, int(track.height * frac))
            travel = track.height - thumb_h
            thumb_y = track.y + int(travel * (self.scroll / float(self._max_scroll)))
            pygame.draw.rect(surface, (105, 146, 214), pygame.Rect(track.x, thumb_y, track.width, thumb_h), border_radius=3)

    def _content_rect(self, panel):
        left_w = 235
        return pygame.Rect(panel.left + left_w + 8, panel.top + 58, panel.width - left_w - 24, panel.height - 76)

    def _visible_line_count(self, content, font):
        body_h = content.height - 46 - 30
        return max(1, body_h // (font.get_linesize() + 2))

    def _wrapped_lines(self, section_id, width, font):
        key = (section_id, width)
        if key in self._wrapped:
            return self._wrapped[key]
        out = []
        for raw in _SECTIONS.get(section_id, []):
            text = raw.strip()
            if not text:
                out.append("")
                continue
            words = text.split(" ")
            cur = ""
            for word in words:
                trial = word if not cur else cur + " " + word
                if font.size(trial)[0] <= width:
                    cur = trial
                    continue
                if cur:
                    out.append(cur)
                cur = word
                # Break overlong words instead of overflowing.
                while font.size(cur)[0] > width and len(cur) > 1:
                    cut = len(cur)
                    while cut > 1 and font.size(cur[:cut])[0] > width:
                        cut -= 1
                    out.append(cur[:cut])
                    cur = cur[cut:]
            if cur:
                out.append(cur)
        self._wrapped[key] = out
        return out
