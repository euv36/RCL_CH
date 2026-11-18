# -*- coding: utf-8 -*-
"""Camera-free checks for T6 UI integration.

Run: python UniKostyl/out/ui_integration_check.py
"""
import os
import sys

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pygame

import unikostyl as U


FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print(f"PASS {name}")
    else:
        print(f"FAIL {name}: {detail}")
        FAILURES.append(name)


class FakeScrap:
    SCRAP_TEXT = "text/plain"

    def __init__(self, payload):
        self.payload = payload
        self.init_calls = 0

    def init(self):
        self.init_calls += 1

    def get(self, _kind):
        return self.payload


def with_fake_scrap(payload, fn):
    old_scrap = getattr(U.pygame, "scrap", None)
    U.pygame.scrap = FakeScrap(payload)
    try:
        return fn()
    finally:
        if old_scrap is not None:
            U.pygame.scrap = old_scrap
        else:
            try:
                delattr(U.pygame, "scrap")
            except AttributeError:
                U.pygame.scrap = old_scrap


def test_clipboard_paste():
    utf8_text = with_fake_scrap(
        "192.168.0.199".encode("utf-8"), U.paste_text_from_clipboard
    )
    check("paste decodes utf-8 clipboard", utf8_text == "192.168.0.199", repr(utf8_text))

    utf16_text = with_fake_scrap(
        "10.132.122.51".encode("utf-16le"), U.paste_text_from_clipboard
    )
    check("paste decodes utf-16le clipboard", utf16_text == "10.132.122.51", repr(utf16_text))

    cleaned = with_fake_scrap(
        b"10.0.0.1\x00\r\n\x01", U.paste_text_from_clipboard
    )
    check("paste strips control/nul chars", cleaned == "10.0.0.1", repr(cleaned))


def test_ctrl_v_event_path():
    old_get = U.pygame.event.get
    old_get_mods = U.pygame.key.get_mods
    old_scrap = getattr(U.pygame, "scrap", None)
    old_text = U.camera_input_text
    old_active = U.input_active
    event = pygame.event.Event(
        pygame.KEYDOWN,
        key=getattr(pygame, "K_v", ord("v")),
        mod=getattr(pygame, "KMOD_CTRL", 64),
        unicode="v",
    )
    U.pygame.event.get = lambda: [event]
    U.pygame.key.get_mods = lambda: getattr(pygame, "KMOD_CTRL", 64)
    U.pygame.scrap = FakeScrap("10.132.122.51".encode("utf-16le"))
    U.camera_input_text = ""
    U.input_active = True
    try:
        U.main_loop_frame(None)
    except Exception as exc:
        check("ctrl+v main-loop event path", False, repr(exc))
    else:
        check(
            "ctrl+v main-loop event path",
            U.camera_input_text == "10.132.122.51",
            repr(U.camera_input_text),
        )
    finally:
        U.pygame.event.get = old_get
        U.pygame.key.get_mods = old_get_mods
        if old_scrap is not None:
            U.pygame.scrap = old_scrap
        else:
            try:
                delattr(U.pygame, "scrap")
            except AttributeError:
                U.pygame.scrap = old_scrap
        U.camera_input_text = old_text
        U.input_active = old_active


def test_hsv_masks_aligned():
    old_thresholds = [list(t) for t in U.thresholds]
    old_masks = dict(U._last_slot_masks)
    try:
        U.thresholds[0] = [100, 100, -128, 127, -128, 127]
        for slot in (1, 2, 3, 4):
            U.thresholds[slot] = list(U.THRESHOLD_DEFAULT)
        frame = np.full((1280, 720, 3), 255, dtype=np.uint8)
        U.compute_track(U.rgb2lab(frame))
        mask = U._last_slot_masks.get(0)
        check("compute_track exports aligned slot mask", mask is not None, "slot 0 missing")
        if mask is not None:
            check("exported mask keeps portrait shape", mask.shape == (1280, 720), mask.shape)
            check("exported mask is 2D grayscale", mask.ndim == 2, mask.ndim)
            check("exported mask has gate pixels", int(mask.sum()) > 0, int(mask.sum()))
    finally:
        for i, t in enumerate(old_thresholds):
            U.thresholds[i] = t
        U._last_slot_masks = old_masks


def test_new_widgets_and_toggles():
    check("HSV button registered", "btn_hsv" in U.widgets and "btn_hsv" in U.wnames)
    check("INFO button registered", "btn_info" in U.widgets and "btn_info" in U.wnames)
    check("HSV window object exists", U._hsv_window is not None)
    check("INFO overlay object exists", U._help is not None)
    if U._help is not None:
        old_visible = U._help.visible
        U.toggle_help("main")
        check("INFO toggle opens main section", U._help.visible is True)
        surface = pygame.Surface((1280, 720))
        try:
            U._help.draw(surface)
        except Exception as exc:
            check("INFO draw at 1280x720", False, repr(exc))
        else:
            check("INFO draw at 1280x720", True)
        U.toggle_help()
        check("INFO toggle closes", U._help.visible is False)
        U._help.visible = old_visible


def main():
    test_clipboard_paste()
    test_ctrl_v_event_path()
    test_hsv_masks_aligned()
    test_new_widgets_and_toggles()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)}")
        return 1
    print("ALL INTEGRATION CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
