import pygame
import numpy as np


def constrain(val, minim, maxim):
    return minim if val < minim else (maxim if val > maxim else val)


def draw_rect_alpha(surface, color, rect):
    shape_surf = pygame.Surface(pygame.Rect(rect).size, pygame.SRCALPHA)
    pygame.draw.rect(shape_surf, color, shape_surf.get_rect())
    surface.blit(shape_surf, rect)


class Widget:
    def __init__(self, screen, rect=pygame.Rect(0, 0, 0, 0), block_click=True):
        self.screen = screen
        self.rect = rect
        self.is_pressed = False
        self.mouse_inside = False
        self.block_click = block_click

    def update(self):
        pass

    def draw(self):
        pass

    def local_pos(self, coords):
        return (coords[0] - self.rect.left, coords[1] - self.rect.top)

    def in_rect(self, coords):
        coords = self.local_pos(coords)
        return (
            coords[0] >= 0
            and coords[0] <= self.rect.width
            and coords[1] >= 0
            and coords[1] <= self.rect.height
        )

    def process_mousedown(self, event):
        if not self.in_rect(event.pos):
            self.is_pressed = False
            return False
        # self.rect.left += 20
        self.is_pressed = True
        return True

    def process_mousemotion(self, event):
        self.mouse_inside = self.in_rect(event.pos)
        pass

    def process_mouseup(self, event):
        self.is_pressed = False
        pass


class ImageNumpy(Widget):
    def __init__(self, screen, rect, source, select_area=False, block_click=True):
        super().__init__(screen, rect, block_click)
        self.rect = rect
        self.pixels = source
        self.image = None
        self.select = select_area
        self.selected_area = None
        self.first_press = None
        # Оптимизация FPS: персистентные поверхности + array_to_surface.
        # Прежний путь make_surface + transform.scale делал ДВЕ полнокадровые
        # копии на каждый кадр (~19 мс на 720x1280): make_surface с astype и
        # аллокацией + scale с новой аллокацией. Теперь — прямой покадровый
        # запис пикселей в переиспользуемый Surface (без astype/аллокаций)
        # и scale в переиспользуемый dest.
        self._src_surf = None   # Surface размера источника (h, w)
        self._disp_surf = None  # Surface размера виджета

    def draw(self):
        src = self.pixels
        if src is not None and hasattr(src, "shape") and src.ndim == 3:
            sw, sh = src.shape[1], src.shape[0]
            if self._src_surf is None or self._src_surf.get_size() != (sw, sh):
                self._src_surf = pygame.Surface((sw, sh))
            try:
                # Прямая запись пикселей (вью (w, h, 3) без копии массива).
                pygame.pixelcopy.array_to_surface(
                    self._src_surf, src.transpose(1, 0, 2)
                )
            except (ValueError, TypeError):
                # Фолбэк на старый путь (другой dtype/компоновка).
                self._src_surf = pygame.surfarray.make_surface(src)
        if self._src_surf is not None:
            if (self._disp_surf is None or
                    self._disp_surf.get_size() != (self.rect.width,
                                                   self.rect.height)):
                self._disp_surf = pygame.Surface(
                    (self.rect.width, self.rect.height)
                )
            pygame.transform.scale(
                self._src_surf, (self.rect.width, self.rect.height),
                self._disp_surf,
            )
            self.screen.blit(self._disp_surf, self.rect.topleft)

        # if self.first_press is not None:
        #    pygame.draw.rect(self.screen, (100, 100, 200, 100), self.selected_area)

    def process_mousedown(self, event):
        if not super().process_mousedown(event):
            return False
        if self.select:
            self.first_press = event.pos
        return True

    def process_mousemotion(self, event):
        if self.first_press is not None:
            mouse_pos = pygame.mouse.get_pos()
            x1, y1 = self.first_press
            x2, y2 = mouse_pos
            if x1 > x2:
                x1 = mouse_pos[0]
                x2 = self.first_press[0]
            if y1 > y2:
                y1 = mouse_pos[1]
                y2 = self.first_press[1]

            self.selected_area = pygame.Rect(self.rect)
            self.selected_area.left = constrain(x1, self.rect.left, self.rect.right)
            self.selected_area.width = (
                min(x2, self.rect.right) - self.selected_area.left
            )
            self.selected_area.top = constrain(y1, self.rect.top, self.rect.bottom)
            self.selected_area.height = (
                min(y2, self.rect.bottom) - self.selected_area.top
            )

    def process_mouseup(self, event):
        self.first_press = None


class Label(Widget):
    def __init__(
        self,
        screen,
        rect,
        text,
        font=None,
        color=(235, 240, 246),
        stratch=True,
        block_click=True,
    ):
        super().__init__(screen, rect, block_click)
        self.text = text
        if font is None:
            font = pygame.font.Font(None, 40)
        self.font = font
        self.color = color
        self.stratch = stratch

    def update(self):
        # self.rect.left += 1
        pass

    def draw(self):
        image = self.font.render(self.text, True, self.color)
        if self.stratch:
            self.screen.blit(
                image,
                (
                    self.rect.left + self.rect.width // 2 - image.get_width() // 2,
                    self.rect.top + self.rect.height // 2 - image.get_height() // 2,
                ),
            )
        else:
            self.screen.blit(image, (self.rect.left, self.rect.top))


class Button(Widget):
    def __init__(
        self,
        screen,
        rect,
        label=None,
        func=None,
        args=(),
        kwargs={},
        colors={
            "normal": (40, 54, 78),
            "hover": (54, 74, 108),
            "pressed": (26, 36, 54),
        },
        block_click=True,
    ):
        super().__init__(screen, rect, block_click)
        self.label = label
        self.func = func
        self.args = args
        self.kwargs = kwargs
        self.colors = colors
        self.state = "normal"
        self.hover = False

    def draw(self):
        state = "pressed" if self.is_pressed else ("hover" if self.hover else "normal")
        clr = self.colors.get(state, self.colors.get("normal", (40, 54, 78)))
        pygame.draw.rect(self.screen, clr, self.rect, border_radius=7)
        pygame.draw.rect(self.screen, (92, 114, 148), self.rect, 1, border_radius=7)
        if self.label is not None:
            self.label.draw()

    def process_mousemotion(self, event):
        self.hover = self.in_rect(event.pos)
        self.mouse_inside = self.hover

    def process_mousedown(self, event):
        if not super().process_mousedown(event):
            self.is_pressed = False
            self.hover = False
            return False
        self.is_pressed = True
        self.hover = True
        self.state = "pressed"
        self.func(*self.args, *self.kwargs)
        return True

    def process_mouseup(self, event):
        self.state = "normal"
        self.is_pressed = False
        if hasattr(event, "pos"):
            self.hover = self.in_rect(event.pos)


class HorizSlider(Widget):
    def __init__(
        self,
        screen,
        rect,
        borders,
        values=(0, 100),
        radius=10,
        color=(100, 100, 100),
        bg=(50, 50, 50),
        bg_width=2,
        block_click=True,
    ):
        super().__init__(screen, rect, block_click)
        self.borders = borders
        self.values = values
        self.radius = radius
        self.color = color
        self.bg = bg
        self.bg_width = bg_width
        self.value = 0
        self.hold = False
        self.rect.width = 2 * self.radius
        self.rect.height = 2 * self.radius  # (self.bg_width + 2)

    def update(self):
        self.rect.left = constrain(
            self.rect.left, self.borders[0] - self.radius, self.borders[1] - self.radius
        )
        if self.borders[1] - self.borders[0] > 0:
            self.value = (self.rect.left + self.radius - self.borders[0]) / (
                self.borders[1] - self.borders[0]
            ) * (self.values[1] - self.values[0]) + self.values[0]
            self.value = round(self.value)

    def draw(self):
        pygame.draw.rect(
            self.screen,
            self.bg,
            pygame.Rect(
                self.borders[0],
                self.rect.top + self.radius - self.bg_width,
                self.borders[1] - self.borders[0],
                self.bg_width * 2,
            ),
        )
        pygame.draw.circle(self.screen, self.color, self.rect.center, self.radius)

    def process_mousedown(self, event):
        if not super().process_mousedown(event):
            return False
        self.hold = True
        return True

    def process_mousemotion(self, event):
        if self.hold:
            self.rect.left = event.pos[0] - self.radius

    def process_mouseup(self, event):
        self.hold = False

    def set_value(self, val):
        val = constrain(val, self.values[0], self.values[1])
        val = int(val)
        self.value = val
        self.rect.left = round(
            (self.value - self.values[0])
            / (self.values[1] - self.values[0])
            * (self.borders[1] - self.borders[0])
            + self.borders[0]
            - self.radius
        )


class ItemList(Widget):
    def __init__(
        self,
        screen,
        rect,
        items,
        padding_x=0,
        padding_y=0,
        offset=(10, 10),
        block_click=True,
    ):
        super().__init__(screen, rect, block_click)
        self.items = items
        self.padding_x = padding_x
        self.padding_y = padding_y
        self.chosen = 0

        if self.padding_x != 0:
            self.rect.width = self.padding_x * len(items)
        if self.padding_y != 0:
            self.rect.height = self.padding_y * len(items)

        for i in range(len(self.items)):
            self.rect.width = max(self.rect.width, self.items[i].rect.width)
            self.rect.height = max(self.rect.height, self.items[i].rect.height)
            self.items[i].rect.left = self.rect.left + i * self.padding_x
            self.items[i].rect.top = self.rect.top + i * self.padding_y
            if self.padding_y != 0:
                self.items[i].rect.height = self.padding_y
            if self.padding_x != 0:
                self.items[i].rect.width = self.padding_x

    def draw(self):
        for i in range(len(self.items)):
            self.items[i].draw()
            if i == self.chosen:
                pygame.draw.rect(self.screen, (150, 100, 100), self.items[i].rect, 2)
        # pygame.draw.rect(self.screen, (30, 30, 30),
        #                  self.rect, 2)

    def process_mousedown(self, event):
        if not super().process_mousedown(event):
            return False

        for i in range(len(self.items)):
            if self.items[i].in_rect(event.pos):
                self.chosen = i

        return True

    def __getitem__(self, i):
        if i < 0 or i >= len(self.items):
            return None
        return self.items[i]
