# -*- coding: utf-8 -*-
"""self_mask.py — Idea 8 (PROMPT_EN.md §5): exclusion of the robot body from
the color search area.

The camera looks down at the robot: the green Raspberry Pi PCB and the black
carbon-fiber tubes sit in the center of view. Without an exclusion zone a
calibrated gate color that resembles the PCB/tubes makes the robot "find a
gate" on itself and spin. Therefore the body zone is cut out of the search
BEFORE the mask is consumed by morphology / connectedComponents / moments —
no piece on the body can ever form a gate object.

Where the mask is applied (all three pipelines, one equation each):
  * Pi (Python)   — rpi_elp_cam.GateTracker.process: per-gate mask right
                    after inRange, before OPEN/CLOSE + clustering;
  * Laptop        — unikostyl.compute_track: per-slot gray mask right after
                    thresholding, before painting and detect_gate;
  * C++ core      — pi_core gate_tracker.cpp: same per-gate zero-out
                    (pure helper self_mask.{hpp,cpp}, unit-tested on PC).

WHY the exclusion zeroes the per-gate MASK and not the HSV/BGR image:
zeroing HSV makes a pixel black (H=0, S=0, V=0), and a gate range that
includes 0 in any channel (dark captured colors have v_low=0; hue ranges
that wrap through 0 include H=0; grey/black has S=0) would still MATCH the
zeroed pixels — the black carbon tubes would leak through exactly in the
worst case. `d <= r` on the mask side is mathematically exact for ANY
configured range. (Same reasoning rules out "paint the BGR frame black".)

Parameters (one schema everywhere):
  * camera_settings.json section "self_mask" — FRACTIONS, so one file serves
    the Pi (640x360 landscape) and the laptop (720x1280 portrait):
        "self_mask": {
            "enabled": true,   # false = no exclusion at all
            "cx": 0.5,         # circle center, fraction of frame WIDTH
            "cy": 0.5,         # fraction of frame HEIGHT
            "r": 0.22,         # inner radius, fraction of min(width, height)
            "r2": 0.0          # outer radius (annulus); 0 = full disk
        }
    The section overrides CLI flags on the Pi (same order as Idea 1 camera
    controls: the file is applied after the flags). Unknown sections of the
    file are preserved by cam_settings.save_settings on rewrite.
  * CLI flags of rpi_elp_cam.py / pi_core — PX in the working frame,
    value <= 0 = auto: circle at the frame center, r = 0.22 * min(w, h)
    (SELF_MASK_R_FRAC, aligned with the `cut center %` RING concept and
    ZONE_RADIUS_FRAC = 0.25 but a SEPARATE entity: ZONE_RADIUS_FRAC is the
    on-screen angle/distance zone, self_mask is the color-search exclusion).
    The user's reference pipeline used the annulus r 110..355 px — at a
    720 px min-dimension that is roughly r=0.15, r2=0.49 in fractions.
    NOTE: with cx/cy != 0.5 the laptop-local path (portrait, rotated 90°
    CCW) and the Pi landscape frame interpret the offset in their own
    frame — keep 0.5/0.5 unless you tune per side.

Rasterization is deliberately NOT cv2.circle: the keep mask is built from
the exact equation dx*dx + dy*dy (IEEE-754 binary64, pixel (x, y) treated
at integer coordinates), identical in Python/numpy and the C++ helper —
bit-exact parity (checked in out/selfmask_check.py). The mask is built ONCE
per parameter/size change, never per frame; the per-frame cost is a single
cv2.bitwise_and per gate (~0.05 ms at 640x360, measured in selfmask_check).

Module is headless-safe and Pi-safe: cv2/numpy only, no pygame, no ZMQ.
Deploys next to rpi_elp_cam.py (pscp, like gate_store.py/gate_cluster.py).
"""

import json
import math
import os

import numpy as np

try:
    import cv2
except ImportError:  # pure logic (parity tests) works without cv2
    cv2 = None


# -------- defaults (PROMPT §5 Idea 8: r ~= 0.22 of min(w, h), centered) --------
SELF_MASK_R_FRAC = 0.22    # inner radius, fraction of min(width, height)
SELF_MASK_R2_FRAC = 0.0    # outer radius; 0 = full disk (no annulus)
SELF_MASK_CX_FRAC = 0.5
SELF_MASK_CY_FRAC = 0.5

# Warn threshold: a radius this large covers almost the whole frame — most
# likely a mistake in cx/cy/r (all gates would be silently invisible).
_WARN_R_FRAC = 0.45

_FRACTION_KEYS = ("cx", "cy", "r", "r2")


# ==================== parameter resolution (pure) ====================
def make_params(width, height, enabled=True, cx=0.0, cy=0.0, r=0.0, r2=0.0):
    """Validated px-level params from raw values (no auto-fill).

    Annulus rule: r2 <= r collapses to r2 = 0 (plain disk of radius r) —
    an "annulus" with the outer radius inside the inner one is a config
    mistake, not a crash. Returns the params dict used everywhere:
    {"enabled", "cx", "cy", "r", "r2"} (px floats; r2 == 0 = no annulus).
    """
    w = max(1, int(width))
    h = max(1, int(height))

    def num(v):
        try:
            if isinstance(v, bool):
                raise ValueError
            v = float(v)
        except (TypeError, ValueError):
            return 0.0
        return v if math.isfinite(v) else 0.0

    cx = num(cx)
    cy = num(cy)
    r = num(r)
    r2 = num(r2)
    if r2 <= r:
        r2 = 0.0
    return {
        "enabled": bool(enabled),
        "cx": cx,
        "cy": cy,
        "r": r,
        "r2": r2,
        "width": w,
        "height": h,
    }


def resolve_params(width, height, cx=None, cy=None, r=None, r2=None,
                   enabled=True):
    """CLI-semantics resolution: None / <= 0 -> defaults (frame center,
    r = SELF_MASK_R_FRAC * min(w, h), no annulus). Mirrors the C++
    resolve_self_mask() 1-in-1 (same constants, same IEEE math)."""
    w = max(1, int(width))
    h = max(1, int(height))

    def auto(v, default):
        if v is None:
            return default
        try:
            v = float(v)
        except (TypeError, ValueError):
            return default
        if not math.isfinite(v) or v <= 0.0:
            return default
        return v

    if cx is None or cx <= 0:
        cx = SELF_MASK_CX_FRAC * w
    if cy is None or cy <= 0:
        cy = SELF_MASK_CY_FRAC * h
    if r is None or r <= 0:
        r = SELF_MASK_R_FRAC * min(w, h)
    return make_params(w, h, enabled=enabled, cx=cx, cy=cy, r=r, r2=r2)


def params_from_fractions(width, height, frac, enabled=None):
    """Fraction dict (camera_settings.json "self_mask" section) -> params.

    Fractions are exact (no auto-fill): r fraction 0.0 == radius 0 == "exclude
    only the exact center pixel"; callers that want a real exclusion set a
    positive r. enabled in the dict wins over the `enabled` argument.
    """
    frac = dict(frac or {})
    if enabled is None:
        enabled = frac.get("enabled", True)
    w = max(1, int(width))
    h = max(1, int(height))
    return make_params(
        w, h,
        enabled=enabled,
        cx=_frac_to_px(frac.get("cx", SELF_MASK_CX_FRAC), w),
        cy=_frac_to_px(frac.get("cy", SELF_MASK_CY_FRAC), h),
        r=_frac_to_px(frac.get("r", SELF_MASK_R_FRAC), min(w, h)),
        r2=_frac_to_px(frac.get("r2", SELF_MASK_R2_FRAC), min(w, h)),
    )


def _frac_to_px(value, dim):
    try:
        if isinstance(value, bool):
            raise ValueError
        value = float(value)
    except (TypeError, ValueError):
        return 0.0
    if not math.isfinite(value):
        return 0.0
    return value * dim


def excluded(params, x, y):
    """PURE point test (mirrors the C++ self_mask_excludes): the pixel (x, y)
    at integer coordinates is inside the exclusion zone."""
    if not params.get("enabled"):
        return False
    dx = float(x) - params["cx"]
    dy = float(y) - params["cy"]
    d2 = dx * dx + dy * dy
    if d2 <= params["r"] * params["r"]:
        return True
    r2 = params["r2"]
    if r2 > 0.0 and d2 >= r2 * r2:
        return True
    return False


def r2_valid(params):
    """True when the params form a real annulus (r2 > 0 after validation)."""
    return bool(params.get("enabled")) and float(params.get("r2", 0.0)) > 0.0


# ==================== camera_settings.json section ====================
def load_section(path):
    """Read the "self_mask" section from camera_settings.json.

    Returns (fractions_dict_or_None, warn_or_None). The missing file is
    normal (defaults apply); a corrupt file or a malformed section gives a
    warning and None — never raises, the robot must not die from JSON
    (PROMPT §2.3).
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None, None
    except Exception as e:
        return None, f"camera_settings.json повреждён ({e}) — self_mask по умолчанию"
    if not isinstance(data, dict):
        return None, None
    section = data.get("self_mask")
    if section is None:
        return None, None
    return parse_section(section)


def parse_section(section):
    """Validate the "self_mask" section (fractions). Returns
    (fractions_dict_or_None, warn_or_None)."""
    if not isinstance(section, dict):
        return None, "секция self_mask в camera_settings.json не словарь — игнорирую"
    out = {}
    enabled = section.get("enabled", True)
    if not isinstance(enabled, bool):
        return None, "self_mask.enabled не bool — секция игнорируется"
    out["enabled"] = enabled
    for key in _FRACTION_KEYS:
        value = section.get(key)
        if value is None:
            continue  # absent -> module default at params_from_fractions
        try:
            if isinstance(value, bool):
                raise ValueError
            value = float(value)
        except (TypeError, ValueError):
            return None, f"self_mask.{key} не число — секция игнорируется"
        if not math.isfinite(value):
            return None, f"self_mask.{key} не конечное число — секция игнорируется"
        if key in ("r", "r2") and value < 0.0:
            return None, f"self_mask.{key} < 0 — секция игнорируется"
        out[key] = value
    warn = None
    r = out.get("r", SELF_MASK_R_FRAC)
    if r >= _WARN_R_FRAC:
        warn = (
            f"self_mask.r={r} (доля min(w,h)) покрывает почти весь кадр — "
            f"проверьте cx/cy/r (ворота будут невидимы)"
        )
    return out, warn


# ==================== keep mask (cv2 level) ====================
def build_keep_mask(params, width, height, out=None):
    """Build the keep mask: 255 = pixel searchable, 0 = excluded (robot body).

    The equation is identical to the C++ build_keep_mask() (IEEE-754
    binary64): excluded iff (x-cx)^2 + (y-cy)^2 <= r^2, plus (for an
    annulus) everything at distance >= r2. Built ONCE per
    parameter/size change — never in the hot loop. `out` (uint8 (h, w))
    is filled in place when given (buffer reuse, no per-frame allocation).
    """
    w = max(1, int(width))
    h = max(1, int(height))
    if out is None:
        out = np.empty((h, w), dtype=np.uint8)
    if not params.get("enabled") or params.get("r", 0.0) <= 0.0:
        out[:] = 255  # nothing excluded
        return out

    cx = float(params["cx"])
    cy = float(params["cy"])
    r = float(params["r"])
    r2 = float(params["r2"])

    # Pixel (x, y) at integer coordinates; same math as C++ (bit-exact):
    # d2 = (x-cx)^2 + (y-cy)^2, keep iff d2 > r^2 and (r2<=0 or d2 < r2^2).
    xs = np.arange(w, dtype=np.float64)
    ys = np.arange(h, dtype=np.float64)
    dx2 = (xs - cx) * (xs - cx)                # (w,)
    dy2 = (ys - cy) * (ys - cy)                # (h,)
    d2 = dx2.reshape(1, w) + dy2.reshape(h, 1)  # (h, w) — one temp per build
    keep = d2 > r * r
    if r2 > 0.0:
        keep &= d2 < r2 * r2
    out[:] = keep.astype(np.uint8)
    out *= np.uint8(255)
    return out


def apply_keep(keep, mask):
    """Zero the exclusion zone of an already computed binary mask
    (in place). One cv2.bitwise_and per gate per frame — the whole
    per-frame cost of the self-mask."""
    if keep is not None:
        cv2.bitwise_and(mask, keep, dst=mask)
    return mask


def status_dict(params):
    """Idea 2 status JSON shape: the applied circle in px (ints), additive
    top-level key "self_mask" -> the laptop parser keeps it in `extra`."""
    if not params or not params.get("enabled"):
        return None
    return {
        "cx": int(round(float(params["cx"]))),
        "cy": int(round(float(params["cy"]))),
        "r": int(round(float(params["r"]))),
        "r2": int(round(float(params["r2"]))),
    }
