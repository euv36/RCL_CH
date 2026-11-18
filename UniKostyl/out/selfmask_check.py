# -*- coding: utf-8 -*-
"""out/selfmask_check.py — camera-free verification of Idea 8 (no hardware).

Covers (PROMPT_EN.md §5 Idea 8 + §6):
  A. self_mask.py units: defaults scale with resolution (640x360 -> 79.2 px,
     1280x720 -> 158.4 px), fractions/section parsing incl. malformed and
     suspicious values, annulus collapse, status_dict shape.
  B. Pi path (rpi_elp_cam.GateTracker on synthetic frames):
     - a green "PCB" blob in the center exclusion zone is NEVER detected
       (including the very first frame — the fresh-tracker immediate-accept
       exception must not fire for masked-out pieces);
     - the same blob outside the zone IS detected;
     - the HSV-zero trap: a dark gate color with s_low=0/v_low=0 + hue wrap
       through 0 matches near-black pixels (carbon tubes) — without the
       mask the black center blob is "found" (bug demo), with the mask it
       is never found (the regression the mask-side zero-out fixes);
     - a blob straddling the boundary: the outside part is tracked, the
       inside part is gone, no crash;
     - annulus: blob between r..r2 detected, inside r excluded, beyond r2
       excluded (PROMPT §6 wording; reference mask r 110..355 px);
     - default tracker WITHOUT set_self_mask keeps the pre-Idea-8 behavior.
  C. Laptop path (unikostyl.compute_track under SDL dummy): center blob not
     tracked and not painted; outside blob tracked; live param update via
     set_self_mask_fractions; RING mode exempt (strip geometry, "cut center"
     owns the body there); load_self_mask_settings reads the section.
  D. Status JSON (Idea 2): additive top-level "self_mask" when enabled,
     absent when off; gates schema unchanged; laptop parse_status_payload
     keeps it in "extra".
  E. camera_settings.json round-trip: cam_settings.save_settings preserves
     the foreign "self_mask" section (Idea 1 contract), load_section reads
     it back.
  F. C++ parity: pi_core_tests --selfmask-parity — keep-mask excluded-pixel
     counts and point probes must match Python bit-for-bit (same IEEE-754
     equation). Skipped with a note if the exe was not built.
  G. Cost: keep-mask build (one-time) and the per-frame apply (one
     bitwise_and per gate) must be trivial (< 0.5 ms/frame added).
  H. Pi-restart-like path: rpi_elp_cam.py --help shows the new flags.

Run:  python out\\selfmask_check.py   (from UniKostyl/, system Python)
"""
import json
import math
import os
import subprocess
import sys
import tempfile
import time

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import self_mask as SM
import gate_cluster as GC
import rpi_elp_cam as R

FAILURES = []
SKIPPED = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


def skip(name, detail=""):
    print(f"[SKIP] {name}" + (f" — {detail}" if detail else ""))
    SKIPPED.append(name)


H, W = 360, 640
CX0, CY0 = W // 2, H // 2


# ============ A. self_mask units ============
print("=== A. self_mask.py: resolution / fractions / sections ===")
p640 = SM.resolve_params(640, 360)
check("defaults 640x360: center + r=0.22*360",
      abs(p640["cx"] - 320.0) < 1e-9 and abs(p640["cy"] - 180.0) < 1e-9
      and abs(p640["r"] - 79.2) < 1e-9 and p640["r2"] == 0.0,
      f"r={p640['r']}")
p720 = SM.resolve_params(1280, 720)
check("defaults scale with resolution (1280x720 -> r=158.4)",
      abs(p720["r"] - 158.4) < 1e-9, f"r={p720['r']}")
pport = SM.resolve_params(720, 1280)  # laptop portrait
check("laptop portrait 720x1280: r from min dim (720)",
      abs(pport["r"] - 158.4) < 1e-9, f"r={pport['r']}")
check("annulus collapse: r2 <= r -> disk",
      SM.resolve_params(640, 360, r=80, r2=80)["r2"] == 0.0
      and SM.resolve_params(640, 360, r=80, r2=50)["r2"] == 0.0)
check("annulus kept: r2 > r",
      SM.resolve_params(640, 360, r=80, r2=200)["r2"] == 200.0)
check("explicit px pass through",
      abs(SM.resolve_params(640, 360, cx=100.5, cy=90, r=33)["cx"] - 100.5) < 1e-9)
check("disabled stays disabled",
      SM.resolve_params(640, 360, enabled=False)["enabled"] is False)

sec, warn = SM.parse_section({"enabled": True, "cx": 0.5, "cy": 0.5,
                              "r": 0.22, "r2": 0.0})
check("section: valid dict parsed", sec is not None and warn is None)
sec, warn = SM.parse_section("junk")
check("section: non-dict -> warn + ignore", sec is None and warn is not None)
sec, warn = SM.parse_section({"enabled": True, "r": "abc"})
check("section: non-numeric r -> warn + ignore",
      sec is None and warn is not None)
sec, warn = SM.parse_section({"enabled": True, "r": 0.9})
check("section: r >= 0.45 warns (covers the frame)",
      sec is not None and warn is not None and "покрывает" in warn)
sec, warn = SM.parse_section({"enabled": True, "cx": True})
check("section: bool is not a number -> warn + ignore",
      sec is None and warn is not None)
s, w = SM.load_section(os.path.join(HERE, "no_such_file_zz.json"))
check("load_section: missing file -> silent defaults", s is None and w is None)

frac, warn = SM.params_f = SM.parse_section({"enabled": True, "r": 0.15,
                                             "r2": 0.49})
pf = SM.params_from_fractions(720, 1280, frac)
check("fractions -> px (reference annulus 0.15..0.49 of 720)",
      abs(pf["r"] - 108.0) < 1e-9 and abs(pf["r2"] - 352.8) < 1e-9,
      f"r={pf['r']:.1f} r2={pf['r2']:.1f}")
check("status_dict ints",
      SM.status_dict(p640) == {"cx": 320, "cy": 180, "r": 79, "r2": 0})
check("status_dict None when disabled",
      SM.status_dict(SM.resolve_params(640, 360, enabled=False)) is None)


# ============ B. Pi path (GateTracker) ============
print("=== B. rpi_elp_cam GateTracker: body zone excluded before inRange ===")

def bgr_frame(blobs, bg=40, blob_bgr=(60, 180, 60)):
    """BGR frame: dark background + blobs [(x0, x1, y0, y1), ...]."""
    f = np.full((H, W, 3), bg, np.uint8)
    for (x0, x1, y0, y1) in blobs:
        f[y0:y1, x0:x1] = blob_bgr
    return f


def make_tracker(self_mask=True, color=(60, 180, 60)):
    tr = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
    if self_mask:
        tr.set_self_mask(SM.resolve_params(W, H))
    tr.update_gate_from_rgb(0, color)
    return tr


# the "PCB" blob in the center: inside r=79.2 of (320, 180)
center_blob = [(300, 340, 160, 200)]
tr_m = make_tracker(self_mask=True)
cv2.cvtColor(bgr_frame(center_blob), cv2.COLOR_BGR2HSV, dst=tr_m.hsv)
info = tr_m.process(0)
check("center blob: first frame NOT detected (no immediate-accept leak)",
      info is None, f"info={info}")
stable_none = all(tr_m.process(0) is None for _ in range(8))
check("center blob: never detected over 8 frames", stable_none)
check("center blob: raw mask zeroed in the zone",
      int(cv2.countNonZero(tr_m.masks[0][160:200, 300:340])) == 0)

# the same blob outside the zone -> detected normally
out_blob = [(480, 520, 60, 100)]
tr_o = make_tracker(self_mask=True)
cv2.cvtColor(bgr_frame(out_blob), cv2.COLOR_BGR2HSV, dst=tr_o.hsv)
info = tr_o.process(0)
check("blob outside the zone detected on the first frame",
      info is not None and abs(info["area_px"] - 1600.0) < 1e-6,
      f"area_px={info['area_px'] if info else '-'}")
check("angle from the real centroid",
      info is not None and info["angle"] ==
      GC.angle_deg(info["cx"], info["cy"], CX0, CY0))

# HSV-zero trap: dark color with s_low=0, v_low=0 and H wrap through 0
# matches near-black pixels (carbon tubes). The mask must kill it.
dark_rgb = (40, 25, 25)
_hsv = cv2.cvtColor(np.uint8([[[dark_rgb[2], dark_rgb[1], dark_rgb[0]]]]),
                    cv2.COLOR_BGR2HSV)[0][0]
check("trap precondition: v_low==0 and s_low==0 and H wraps",
      _hsv[2] <= 100 and _hsv[1] <= 100 and _hsv[0] <= 40,
      f"HSV={list(map(int, _hsv))}")
black_center = [(296, 344, 156, 204)]        # near-black tubes on the body
# Bright background (= floor in the real scene): the dark color's v_high is
# 140, so the floor itself never matches and the only matching pixels are
# the black tubes in the center — exactly the HSV-zero trap.
tr_trap_off = make_tracker(self_mask=False, color=dark_rgb)
cv2.cvtColor(bgr_frame(black_center, bg=200, blob_bgr=(20, 20, 20)),
             cv2.COLOR_BGR2HSV, dst=tr_trap_off.hsv)
info_off = tr_trap_off.process(0)
check("WITHOUT mask the black body blob IS a 'gate' (bug demo)",
      info_off is not None
      and abs(info_off["area_px"] - 48.0 * 48.0) < 1e-6,
      f"area_px={info_off['area_px'] if info_off else '-'}")
tr_trap_on = make_tracker(self_mask=True, color=dark_rgb)
cv2.cvtColor(bgr_frame(black_center, bg=200, blob_bgr=(20, 20, 20)),
             cv2.COLOR_BGR2HSV, dst=tr_trap_on.hsv)
check("WITH mask the black body blob is never a gate",
      all(tr_trap_on.process(0) is None for _ in range(8)))

# blob straddling the boundary: outside part tracked, inside part gone
straddle = [(300, 420, 170, 190)]            # 120x20, center 40 px inside
tr_s = make_tracker(self_mask=True)
cv2.cvtColor(bgr_frame(straddle), cv2.COLOR_BGR2HSV, dst=tr_s.hsv)
info = tr_s.process(0)
check("straddling blob: outside part tracked, no crash",
      info is not None, f"info={info}")
if info is not None:
    check("straddling blob: area < full blob (inside part removed)",
          0.0 < info["area_px"] < 2400.0, f"area_px={info['area_px']}")
    check("straddling blob: centroid outside the zone",
          math.hypot(info["cx"] - CX0, info["cy"] - CY0) > p640["r"],
          f"cx={info['cx']}")

# annulus (reference semantics: search ring r..r2) — a FRESH tracker per
# case so the hysteresis "held" publication cannot blur the expectations
ann_params = SM.resolve_params(W, H, r=79.2, r2=240.0)
cases = [
    ("inside inner r", [(300, 340, 160, 200)], False),    # d ~ 28..45
    ("between r and r2", [(140, 180, 160, 200)], True),   # d ~ 140..180
    ("beyond r2", [(40, 80, 160, 200)], False),           # d ~ 240..280
]
for name, blobs, want in cases:
    tr_a = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
    tr_a.set_self_mask(ann_params)
    tr_a.update_gate_from_rgb(0, (60, 180, 60))
    cv2.cvtColor(bgr_frame(blobs), cv2.COLOR_BGR2HSV, dst=tr_a.hsv)
    info = tr_a.process(0)
    check(f"annulus: {name} -> {'detected' if want else 'excluded'}",
          (info is not None) == want,
          f"found={info is not None} area_px={info['area_px'] if info else '-'}")

# pre-Idea-8 behavior preserved when the mask is not installed
tr_n = make_tracker(self_mask=False)
cv2.cvtColor(bgr_frame(center_blob), cv2.COLOR_BGR2HSV, dst=tr_n.hsv)
check("no set_self_mask -> old behavior (center blob found, regression guard)",
      tr_n.process(0) is not None)


# ============ C. laptop path (unikostyl.compute_track, SDL dummy) ============
print("=== C. unikostyl.compute_track: laptop local path ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U


def lab_range_for(frame_rgb, x0, x1, y0, y1, pad=25):
    lab = U.rgb2lab(frame_rgb)[y0:y1, x0:x1].reshape(-1, 3)
    return [int(lab[:, 0].min()) - pad, int(lab[:, 0].max()) + pad,
            int(lab[:, 1].min()) - pad, int(lab[:, 1].max()) + pad,
            int(lab[:, 2].min()) - pad, int(lab[:, 2].max()) + pad]

U.thresholds[0] = [0, 100, -128, 127, -128, 127]  # reset slot 0 first
frame_rgb = np.full((H, W, 3), 40, np.uint8)
frame_rgb[160:200, 300:340] = (60, 180, 60)       # green blob in the center
frame_rgb[60:100, 480:520] = (60, 180, 60)        # same green outside
U.thresholds[0] = lab_range_for(frame_rgb, 300, 340, 160, 200)

for st in U._track_stability:
    st.reset()
comb, res, tgt, ang = U.compute_track(U.rgb2lab(frame_rgb))
r0 = next(r for r in res if r["slot"] == 0)
# The center blob is excluded; the outside blob is a legitimate find — the
# record must either be empty or point at the OUTSIDE blob, never at the
# center one (d of the found centroid from (320, 180) must exceed r).
check("laptop: center blob NOT tracked (found centroid outside the zone "
      "or nothing found)",
      (not r0["found"])
      or math.hypot(r0["cx"] - CX0, r0["cy"] - CY0) > p640["r"],
      f"rec={ {k: r0[k] for k in ('found', 'cx', 'cy')} }")
# the center axes/zone decorations are drawn over `combined`, so check for
# the TRACK COLOR specifically (yellow = TRACK_COLORS[0]) in the blob box
blob_painted = int(np.count_nonzero(
    np.all(comb[160:200, 300:340] == U.TRACK_COLORS[0], axis=2)))
check("laptop: center blob NOT painted in the combined mask",
      blob_painted == 0, f"painted px={blob_painted}")
# the outside blob must be findable: recompute with only it visible
for st in U._track_stability:
    st.reset()
frame_only_out = np.full((H, W, 3), 40, np.uint8)
frame_only_out[60:100, 480:520] = (60, 180, 60)
comb2, res2, tgt2, ang2 = U.compute_track(U.rgb2lab(frame_only_out))
r0b = next(r for r in res2 if r["slot"] == 0)
check("laptop: blob outside the zone detected (first frame)",
      r0b["found"] and abs(r0b["area"] - 1600.0) < 1e-6,
      f"area={r0b['area']}")

# live param update hook (future CAM SET trackbars): widen r -> blob excluded
U.set_self_mask_fractions({"enabled": True, "cx": 0.5, "cy": 0.5,
                           "r": 0.45, "r2": 0.0})
for st in U._track_stability:
    st.reset()
comb3, res3, tgt3, ang3 = U.compute_track(U.rgb2lab(frame_only_out))
r0c = next(r for r in res3 if r["slot"] == 0)
# blob center (500, 80): d from center = sqrt(180^2 + 100^2) = ~205.9
# r = 0.45*360 = 162 -> still outside; shift: use a near-center point check
check("laptop: r=0.45 keeps the far blob visible",
      r0c["found"], f"area={r0c['area']}")
U.set_self_mask_fractions({"enabled": True, "cx": 0.5, "cy": 0.5,
                           "r": 0.8, "r2": 0.0})
for st in U._track_stability:
    st.reset()
comb4, res4, tgt4, ang4 = U.compute_track(U.rgb2lab(frame_only_out))
r0d = next(r for r in res4 if r["slot"] == 0)
check("laptop: r=0.8 (288 px) swallows the blob at d=206 -> excluded",
      not r0d["found"], f"found={r0d['found']}")
# restore defaults
U.set_self_mask_fractions({"enabled": True, "cx": 0.5, "cy": 0.5,
                           "r": 0.22, "r2": 0.0})

# RING mode exempt: strip geometry, the body is cut by "cut center %"
U.fisheye_mode = 2
try:
    for st in U._track_stability:
        st.reset()
    comb5, res5, tgt5, ang5 = U.compute_track(U.rgb2lab(frame_rgb))
    r0e = next(r for r in res5 if r["slot"] == 0)
    check("laptop RING: self-mask NOT applied (center blob visible as before)",
          r0e["found"], f"found={r0e['found']}")
finally:
    U.fisheye_mode = 0
for st in U._track_stability:
    st.reset()

# loader: section from a temp camera_settings.json
tmpdir = tempfile.mkdtemp(prefix="selfmask_check_")
cfg_path = os.path.join(tmpdir, "camera_settings.json")
with open(cfg_path, "w", encoding="utf-8") as f:
    json.dump({"version": 1, "controls": {"exposure": 300},
               "self_mask": {"enabled": True, "cx": 0.4, "cy": 0.6,
                             "r": 0.3, "r2": 0.5}}, f)
warn = U.load_self_mask_settings(cfg_path)
check("laptop loader: section applied to SELF_MASK",
      U.SELF_MASK.get("cx") == 0.4 and U.SELF_MASK.get("r2") == 0.5
      and warn is None, f"SELF_MASK={U.SELF_MASK}")
U.set_self_mask_fractions({"enabled": True, "cx": 0.5, "cy": 0.5,
                           "r": 0.22, "r2": 0.0})


# ============ D. status JSON (Idea 2, additive) ============
print("=== D. build_gate_status: additive self_mask key ===")
from status_panel import parse_status_payload

tr_st = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
st = R.build_gate_status(tr_st, {}, 25.9)
check("status: no self_mask key when the mask is off",
      "self_mask" not in st, json.dumps(st))
tr_st.set_self_mask(SM.resolve_params(W, H))
st = R.build_gate_status(tr_st, {}, 25.9)
check("status: self_mask circle reported when enabled",
      st.get("self_mask") == {"cx": 320, "cy": 180, "r": 79, "r2": 0},
      json.dumps(st.get("self_mask")))
check("status: gates schema intact (enabled/found only here)",
      set(st["gates"]["1"].keys()) == {"enabled", "found"},
      json.dumps(st["gates"]["1"]))
tr_st.update_gate_from_rgb(0, (255, 255, 0))
cv2.cvtColor(bgr_frame(out_blob), cv2.COLOR_BGR2HSV, dst=tr_st.hsv)
info = tr_st.process(0)
st_full = R.build_gate_status(tr_st, {0: info}, 25.9)
parsed = parse_status_payload(json.dumps(st_full).encode())
check("laptop parser: self_mask lands in extra",
      parsed["extra"].get("self_mask", {}).get("r") == 79,
      json.dumps(parsed["extra"]))
check("laptop parser: gate fields intact",
      parsed["gates"]["1"]["found"] is True
      and "angle" in parsed["gates"]["1"], json.dumps(parsed["gates"]["1"]))


# ============ E. camera_settings.json round-trip ============
print("=== E. camera_settings.json: section survives cam_settings writes ===")
import cam_settings as CS

cfg2 = os.path.join(tmpdir, "camera_settings_2.json")
CS.save_settings(cfg2, {"exposure": 300, "gain": 0})
with open(cfg2, "r", encoding="utf-8") as f:
    data = json.load(f)
data["self_mask"] = {"enabled": True, "cx": 0.5, "cy": 0.5, "r": 0.3,
                     "r2": 0.0}
with open(cfg2, "w", encoding="utf-8") as f:
    json.dump(data, f)
CS.save_settings(cfg2, {"exposure": 350})       # Idea 1 rewrite
with open(cfg2, "r", encoding="utf-8") as f:
    data2 = json.load(f)
check("Idea 1 save_settings preserves the self_mask section",
      data2.get("self_mask", {}).get("r") == 0.3
      and data2["controls"]["exposure"] == 350,
      json.dumps(data2.get("self_mask")))
sec, warn = SM.load_section(cfg2)
check("self_mask.load_section reads the preserved section",
      sec is not None and abs(sec["r"] - 0.3) < 1e-9 and warn is None)
# malformed section in a real file -> warn, no crash
cfg3 = os.path.join(tmpdir, "camera_settings_3.json")
with open(cfg3, "w", encoding="utf-8") as f:
    json.dump({"controls": {}, "self_mask": {"r": "oops"}}, f)
sec, warn = SM.load_section(cfg3)
check("load_section: malformed -> warn + None", sec is None and warn)


# ============ F. C++ parity ============
print("=== F. C++ parity (pi_core_tests --selfmask-parity) ===")
exe = os.path.join(HERE, "pi_core_build_win", "pi_core_tests.exe")
if not os.path.exists(exe):
    skip("C++ parity: pi_core_tests.exe not built",
         "rebuild with python -m ziglang c++ (see COORDINATION notes)")
else:
    cases = [
        # (w, h, cx, cy, r, r2, enabled) — resolved px values
        (W, H, 320.0, 180.0, 79.2, 0.0, 1),
        (W, H, 320.0, 180.0, 79.2, 240.0, 1),
        (720, 1280, 360.0, 640.0, 158.4, 0.0, 1),
        (W, H, 320.0, 180.0, 79.2, 0.0, 0),
        (100, 80, 50.0, 40.0, 30.0, 60.0, 1),
    ]
    lines = []
    for (w_, h_, cx_, cy_, r_, r2_, en_) in cases:
        lines.append(f"case {w_} {h_} {cx_} {cy_} {r_} {r2_} {en_}")
        pp = {"enabled": bool(en_), "cx": cx_, "cy": cy_, "r": r_, "r2": r2_}
        probes = [
            (cx_, cy_), (cx_ + r_ / 2, cy_), (cx_, cy_ - r_ / 2),
            (cx_ + r_, cy_), (cx_ + r_ + 1.0, cy_),
            (cx_ + (r_ + r2_) / 2 if r2_ > r_ else cx_, cy_),
            (cx_ + r2_, cy_) if r2_ > 0 else (cx_, cy_),
            (cx_ + r2_ + 1.0, cy_) if r2_ > 0 else (cx_, cy_),
            (1.0, 1.0), (w_ - 1.0, h_ - 1.0),
        ]
        for (px_, py_) in probes:
            lines.append(f"probe {px_} {py_}")
    parity_file = os.path.join(tmpdir, "selfmask_parity.txt")
    with open(parity_file, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    proc = subprocess.run([exe, "--selfmask-parity", parity_file],
                          capture_output=True, text=True, timeout=120)
    cpp_counts = []
    cpp_probes = []
    for ln in proc.stdout.splitlines():
        if ln.startswith("case ") and "excluded=" in ln:
            cpp_counts.append(int(ln.split("excluded=")[1]))
        elif ln.startswith("probe "):
            _, xs, ys, v = ln.split()
            cpp_probes.append((float(xs), float(ys), int(v)))
    py_counts = []
    py_probes = []
    for (w_, h_, cx_, cy_, r_, r2_, en_) in cases:
        pp = {"enabled": bool(en_), "cx": cx_, "cy": cy_, "r": r_, "r2": r2_}
        km = SM.build_keep_mask(pp, w_, h_)
        py_counts.append(int(np.count_nonzero(km == 0)))
        probes = [
            (cx_, cy_), (cx_ + r_ / 2, cy_), (cx_, cy_ - r_ / 2),
            (cx_ + r_, cy_), (cx_ + r_ + 1.0, cy_),
            (cx_ + (r_ + r2_) / 2 if r2_ > r_ else cx_, cy_),
            (cx_ + r2_, cy_) if r2_ > 0 else (cx_, cy_),
            (cx_ + r2_ + 1.0, cy_) if r2_ > 0 else (cx_, cy_),
            (1.0, 1.0), (w_ - 1.0, h_ - 1.0),
        ]
        for (px_, py_) in probes:
            py_probes.append((px_, py_, 1 if SM.excluded(pp, px_, py_) else 0))
    check("C++/Python excluded-pixel counts match (5 cases)",
          cpp_counts == py_counts,
          f"cpp={cpp_counts} py={py_counts}")
    check("C++/Python point probes match",
          cpp_probes == py_probes,
          f"{len(cpp_probes)} probes")


# ============ G. cost ============
print("=== G. cost of the self-mask ===")
p = SM.resolve_params(W, H)
km = SM.build_keep_mask(p, W, H)
m = np.full((H, W), 255, np.uint8)
N = 2000
t0 = time.perf_counter()
for _ in range(N):
    cv2.bitwise_and(m, km, dst=m)
per_and = (time.perf_counter() - t0) / N * 1000.0
check("per-gate apply (bitwise_and 640x360) trivial",
      per_and < 0.2, f"{per_and:.4f} ms -> 3 gates {3 * per_and:.3f} ms/frame")

t0 = time.perf_counter()
for _ in range(200):
    SM.build_keep_mask(p, W, H)
build_ms = (time.perf_counter() - t0) / 200 * 1000.0
check("keep-mask build (one-time) trivial",
      build_ms < 5.0, f"{build_ms:.3f} ms at 640x360")

# end-to-end: process() with the mask vs without on identical frames.
# Interleaved rounds, minimum per phase (noise-robust benchmarking): the
# raw apply is ~0.026 ms/gate, so run-to-run scheduler noise easily exceeds
# it — the MINIMUM over rounds is the honest cost estimate.
frames = [bgr_frame([(140, 180, 160, 200), (480, 520, 60, 100)])
          for _ in range(200)]
tr_a2 = make_tracker(self_mask=False)
tr_b2 = make_tracker(self_mask=True)

def phase(tr, frames_):
    t0 = time.perf_counter()
    for f_ in frames_:
        cv2.cvtColor(f_, cv2.COLOR_BGR2HSV, dst=tr.hsv)
        tr.process(0)
    return (time.perf_counter() - t0) / len(frames_) * 1000.0

# warm-up both
phase(tr_a2, frames[:40])
phase(tr_b2, frames[:40])
t_without = min(phase(tr_a2, frames) for _ in range(3))
t_with = min(phase(tr_b2, frames) for _ in range(3))
delta = t_with - t_without
check("process() delta with the mask < 0.5 ms/frame",
      delta < 0.5, f"min without={t_without:.2f} ms, min with={t_with:.2f} ms, "
                   f"delta={delta:+.3f} ms")

# laptop-size build cost (720x1280 portrait)
t0 = time.perf_counter()
for _ in range(50):
    SM.build_keep_mask(pport, 720, 1280)
build_big = (time.perf_counter() - t0) / 50 * 1000.0
check("keep-mask build at 720x1280 (one-time) acceptable",
      build_big < 15.0, f"{build_big:.2f} ms")


# ============ H. Pi-restart-like path (--help) ============
print("=== H. rpi_elp_cam.py --help shows the new flags ===")
proc = subprocess.run([sys.executable, "rpi_elp_cam.py", "--help"],
                      capture_output=True, text=True,
                      cwd=os.path.join(HERE, ".."), timeout=60)
help_text = proc.stdout + proc.stderr
for flag in ("--self-cx", "--self-cy", "--self-r", "--self-r2",
             "--no-self-mask"):
    check(f"--help lists {flag}", flag in help_text)
check("--help exit code 0", proc.returncode == 0, str(proc.returncode))


print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print(f"RESULT: all checks passed"
      + (f" (skipped: {SKIPPED})" if SKIPPED else ""))
