"""T5 checks for the Russian INFO overlay and content.

Run: python out/help_check.py
"""
import os
import sys

import pygame

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import help_content
from help_overlay import HelpOverlay

FAILED = 0


def check(name, cond):
    global FAILED
    if cond:
        print(f"  PASS {name}")
    else:
        FAILED = 1
        print(f"  FAIL {name}")


def combined_text():
    return "\n".join(line for section in help_content.SECTIONS for line in section["lines"])


def main():
    print("T5 help_check")
    required_ids = ["main", "gates", "radar", "camera", "hsv", "field", "status"]
    ids = [s["id"] for s in help_content.SECTIONS]
    check("all required sections present in stable order", ids == required_ids)
    check("every section has id/title/lines", all(isinstance(s.get("title"), str) and s["title"].strip() for s in help_content.SECTIONS))
    check("content is in Russian and non-empty", all(len(s["lines"]) >= 8 and any(any(ch in "абвгдеёжзийклмнопрстуфхцчшщъыьэюя" for ch in line.lower()) for line in s["lines"]) for s in help_content.SECTIONS))

    text = combined_text()
    for label in [
        "CONNECT", "Ctrl+V", "Bitmap", "L", "A", "B", "SAVE G1", "TARGET",
        "GATE", "RADAR", "CAM SET", "HSV MASK", "BIND FIELD", "POS:", "INFO",
        "FPS", "PUB 5557", "L / A / B", "R", "G2", "ESC",
    ]:
        check(f"content mentions {label!r}", label in text)
    check("help explains every camera-control family", all(x in text for x in ["Exposure", "Gain", "Brightness", "Gamma", "Saturation", "Contrast", "Power line", "Dynamic framerate"]))
    check("help explains radar closing and gate selection", "крестику" in text and "слот" in text)

    pygame.init()
    screen = pygame.Surface((1280, 720))
    overlay = HelpOverlay((1280, 720))
    check("overlay starts hidden", overlay.visible is False)
    overlay.toggle("gates")
    check("toggle shows requested section", overlay.visible is True and overlay.section_id == "gates")

    # Tab click on Radar tab.
    radar_tab_rect = overlay.tab_rects().get("radar")
    check("tab rects are available", radar_tab_rect is not None)
    consumed = overlay.handle_event(pygame.event.Event(pygame.MOUSEBUTTONDOWN, {"button": 1, "pos": radar_tab_rect.center}))
    check("click on tab is consumed and switches section", consumed is True and overlay.section_id == "radar")

    # Wheel scrolls and clamps (resize to force overflow).
    overlay.set_screen_size((850, 600))
    overlay.toggle("camera")
    overlay.scroll = 0
    consumed = overlay.handle_event(pygame.event.Event(pygame.MOUSEWHEEL, {"y": -1}))
    check("wheel down scrolls forward", consumed is True and overlay.scroll >= 1)
    for _ in range(1000):
        overlay.handle_event(pygame.event.Event(pygame.MOUSEWHEEL, {"y": -1}))
    max_scroll = overlay.scroll
    overlay.handle_event(pygame.event.Event(pygame.MOUSEWHEEL, {"y": 1}))
    check("scroll reaches finite clamp", max_scroll > 0 and overlay.scroll <= max_scroll)

    # Draw at two sizes without raising.
    overlay.draw(screen)
    check("draw at 1280x720 produced non-blank pixels", screen.get_rect().width == 1280 and pygame.Surface.get_at(screen, (10, 10)).r >= 0)
    small = pygame.Surface((850, 600))
    overlay.set_screen_size((850, 600))
    overlay.draw(small)
    check("resize to 850x600 re-lays out and draws", small.get_width() == 850 and overlay.screen_size == (850, 600))

    # Close interactions.
    overlay.toggle("main")
    consumed = overlay.handle_event(pygame.event.Event(pygame.KEYDOWN, {"key": pygame.K_ESCAPE}))
    check("ESC closes overlay and is consumed", consumed is True and overlay.visible is False)
    overlay.toggle()
    consumed = overlay.handle_event(pygame.event.Event(pygame.MOUSEBUTTONDOWN, {"button": 1, "pos": overlay.close_rect().center}))
    check("close button closes overlay", consumed is True and overlay.visible is False)

    pygame.quit()
    if FAILED:
        print("T5 help_check: FAILURES")
        raise SystemExit(1)
    print("T5 help_check: all checks passed")


if __name__ == "__main__":
    main()
