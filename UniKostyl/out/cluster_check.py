# -*- coding: utf-8 -*-
"""out/cluster_check.py — camera-free verification of Idea 5 (no hardware).

Covers (PROMPT_EN.md §6, synthetic frames):
  A. gate_cluster pure functions: two pieces with a gap merge into ONE gate,
     a foreign object of the same color in another corner is discarded,
     min_area drops small bits, candidate cap, angle/radius stability under
     1-px jitter, anchor preference towards the previous position.
  B. rpi_elp_cam.GateTracker full pipeline on synthetic HSV frames:
     exactly one merged gate object per color, stray rejected, --open-kernel
     removes speckles at the mask level, min-area scaling.
  C. Serial contract with hysteresis (stub serial link, byte-exact lines):
     G<id>,<angle>,<area> on >=3 deg WRAPPED change with MIN_SEND_INTERVAL,
     G<id>,-1,0 exactly once and only after N empty frames, re-acception
     after M frames.
  D. T-shirt-like blob does NOT steal the tracking while the gate is
     visible; a stable replacement takes over only after M frames when the
     gate is gone (documented semantics).
  E. Laptop path (unikostyl.compute_track under SDL dummy): same semantics
     for the local-camera view — merged gate, held flag during grace,
     loss after N empty frames.

Run:  python out\\cluster_check.py   (from UniKostyl/, system Python)
"""
import math
import os
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import gate_cluster as GC
import rpi_elp_cam as R

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


H, W = 360, 640
CX0, CY0 = W // 2, H // 2


def comp(cx, cy, area, x, y, w, h):
    return (float(cx), float(cy), float(area), float(x), float(y),
            float(w), float(h))


# ============ A. pure clustering ============
print("=== A. gate_cluster: merge / stray / min-area / stability ===")
params = GC.resolve_params(W, H, min_area_px=100)
check("resolve: merge_gap default 2.5% of width",
      params["merge_gap_px"] == 16.0, f"{params['merge_gap_px']}")
check("resolve: max_stray default 10% of width",
      params["max_stray_px"] == 64.0, f"{params['max_stray_px']}")
check("resolve: even open kernel -> odd", GC.resolve_params(W, H, open_kernel=4)["open_kernel"] == 5)

# two pieces with a 16 px gap (the opponent's body) + stray in a corner
comps = [
    comp(240, 120, 3200, 200, 100, 80, 40),    # piece 1
    comp(338, 120, 3360, 296, 100, 84, 40),    # piece 2, gap = 296-280 = 16
    comp(65, 320, 2000, 40, 300, 50, 40),      # stray "t-shirt", far away
]
det = GC.build_gate_object(comps, params)
check("two pieces merged into one gate", det is not None and det["n_components"] == 2,
      f"n_components={det['n_components'] if det else '-'}")
check("stray not part of the gate", det is not None and det["area"] == 6560.0,
      f"area={det['area'] if det else '-'}")
w_centroid = (3200 * 240 + 3360 * 338) / 6560.0
check("centroid = exact area-weighted mean", abs(det["cx"] - w_centroid) < 1e-9,
      f"cx={det['cx']:.2f} want {w_centroid:.2f}")
check("union box covers both pieces", det["x"] == 200 and det["w"] == 180)

# stray alone: no prev -> largest cluster wins, far piece is dropped
det2 = GC.build_gate_object([comps[0], comps[2]], params)
check("no prev: largest cluster is the gate, stray discarded",
      det2 is not None and det2["area"] == 3200.0, f"area={det2['area']}")

# anchor preference: prev near the small cluster -> it becomes the gate
det3 = GC.build_gate_object([comps[0], comps[2]], params,
                            prev_x=comps[2][0], prev_y=comps[2][1])
check("prev position: nearer cluster becomes the anchor",
      det3 is not None and det3["area"] == 2000.0, f"area={det3['area']}")

# min-area is applied by detect_gate on the mask level; build_gate_object
# receives already-filtered components. Check the filter through detect_gate.
mask = np.zeros((H, W), np.uint8)
mask[100:140, 200:280] = 255       # 3200 px
mask[100:140, 296:380] = 255       # 3360 px
mask[100:110, 500:505] = 255       # 50 px small bit, far
mask[300:340, 40:90] = 255         # stray 2000 px
labels = np.empty((H, W), np.int32)
d = GC.detect_gate(mask, params, labels=labels)
check("detect: small bits (< min_area) never become candidates",
      d is not None and d["n_components"] == 2 and d["area"] == 6560.0,
      f"n={d['n_components'] if d else '-'} area={d['area'] if d else '-'}")

p0 = GC.resolve_params(W, H, min_area_px=0)
d0 = GC.detect_gate(mask, p0, labels=labels)
check("detect: min_area=0 keeps the small bit as a candidate (then stray-rejected)",
      d0 is not None and d0["n_components"] == 2 and d0["area"] == 6560.0)

# angle and radius stability under 1-px jitter (same object, shifted mask)
angles = []
radii = []
for dx, dy in ((0, 0), (1, 0), (0, 1), (-1, -1), (1, 1)):
    m = np.zeros((H, W), np.uint8)
    m[100 + dy:140 + dy, 200 + dx:280 + dx] = 255
    m[100 + dy:140 + dy, 296 + dx:380 + dx] = 255
    dd = GC.detect_gate(m, params, labels=labels)
    angles.append(GC.angle_deg(dd["cx"], dd["cy"], CX0, CY0))
    radii.append(dd["radius"])
check("angle stable (<=1 deg) under 1-px jitter", max(angles) - min(angles) <= 1,
      f"angles={angles}")
check("radius stable (<=1.5 px) under 1-px jitter",
      max(radii) - min(radii) <= 1.5, f"radii={[round(r, 1) for r in radii]}")

# radius = circumcircle of the union box (documented upper bound of
# minEnclosingCircle): single 180x40 box -> 0.5*sqrt(180^2+40^2)
want_r = 0.5 * math.sqrt(180.0 ** 2 + 40.0 ** 2)
check("radius = union-box circumcircle (parity definition)",
      abs(det["radius"] - want_r) < 1e-9, f"r={det['radius']:.2f}")

# candidate cap: 300 identical gate-like overlapping components -> 256 processed
many = [comp(100, 100, 100, 90, 90, 20, 20) for _ in range(300)]
dmany = GC.build_gate_object(many, params)
check("candidate cap (300 -> 256) still yields a gate object",
      dmany is not None and dmany["n_components"] == GC.MAX_CANDIDATES,
      f"n={dmany['n_components'] if dmany else '-'}")

# gap exactly at merge_gap merges (<=), one px more does not
a = comp(240, 120, 3200, 200, 100, 80, 40)
b16 = comp(338, 120, 100, 296, 100, 84, 40)   # gap 16 <= 16 -> merge
b17 = comp(339, 120, 100, 297, 100, 84, 40)   # gap 17 > 16 -> separate
check("gap == merge_gap merges", GC.build_gate_object([a, b16], params)["n_components"] == 2)
check("gap > merge_gap stays separate (then stray-rejected)",
      GC.build_gate_object([a, b17], params)["n_components"] == 1)

# ============ B. GateTracker pipeline (synthetic HSV frames) ============
print("=== B. rpi_elp_cam GateTracker: one merged object per color ===")
tracker = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tracker.update_gate_from_rgb(0, (255, 255, 0))   # yellow -> H=30


def gate_frame(pieces, stray=True, speckle=False):
    """BGR frame: dark background + yellow pieces + same-color stray."""
    f = np.full((H, W, 3), 40, np.uint8)
    for (x0, x1) in pieces:
        f[100:140, x0:x1] = (0, 255, 255)          # yellow BGR
    if stray:
        f[300:340, 40:90] = (0, 255, 255)          # same color, other corner
    if speckle:
        f[50:52, 600:602] = (0, 255, 255)          # 2x2 speckle
    return f


frame = gate_frame([(200, 280), (296, 380)])
cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
info = tracker.process(0)
check("gate found on synthetic frame", info is not None)
check("exactly one object: stray of the same color rejected",
      info is not None and abs(info["area_px"] - 6560.0) < 1e-6,
      f"area_px={info['area_px'] if info else '-'}")
check("angle from merged centroid", info is not None and info["angle"] == GC.angle_deg(info["cx"], info["cy"], CX0, CY0))
check("area scaled to 1080p", info is not None and abs(info["area"] - 6560.0) < 1e-6)
check("radius present", info is not None and info["radius"] > 0,
      f"radius={info['radius'] if info else '-'}")
check("w_px = cluster width", info is not None and info["w_px"] == 180)

# frame-to-frame stability: identical input -> identical output
stability_ok = True
for _ in range(10):
    i2 = tracker.process(0)
    if i2["angle"] != info["angle"] or abs(i2["radius"] - info["radius"]) > 1e-9 \
            or i2["cx"] != info["cx"]:
        stability_ok = False
        break
check("angle/size stable frame to frame (identical input)", stability_ok)

# jittered frames: angle wobbles <=2 deg
jit_angles = []
for dx, dy in ((0, 0), (1, 0), (0, 1), (-1, 0)):
    fr = np.roll(np.roll(gate_frame([(200, 280), (296, 380)]), dy, axis=0), dx, axis=1)
    cv2.cvtColor(fr, cv2.COLOR_BGR2HSV, dst=tracker.hsv)
    ii = tracker.process(0)
    if ii is not None:
        jit_angles.append(ii["angle"])
check("angle wobble <=2 deg under 1-px frame shifts",
      max(jit_angles) - min(jit_angles) <= 2, f"{jit_angles}")

# --open-kernel: 2x2 speckle removed from the mask by OPEN(3x3)
tracker3 = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1, open_kernel=3)
tracker3.update_gate_from_rgb(0, (255, 255, 0))
frame_sp = gate_frame([(200, 280), (296, 380)], speckle=True)
cv2.cvtColor(frame_sp, cv2.COLOR_BGR2HSV, dst=tracker3.hsv)
tracker3.process(0)
check("OPEN(3x3) removes 2x2 speckle from the mask",
      int(cv2.countNonZero(tracker3.masks[0][50:52, 600:602])) == 0)
check("gate still found with OPEN(3x3)", tracker3.process(0) is not None)

tracker1 = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1, open_kernel=1)
tracker1.update_gate_from_rgb(0, (255, 255, 0))
cv2.cvtColor(frame_sp, cv2.COLOR_BGR2HSV, dst=tracker1.hsv)
tracker1.process(0)
check("open_kernel=1 keeps the speckle in the raw mask (OPEN disabled)",
      int(cv2.countNonZero(tracker1.masks[0][50:52, 600:602])) == 4)

# min_area: pieces below the threshold are not a gate
tracker_min = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tracker_min.update_gate_from_rgb(0, (255, 255, 0))
small = np.full((H, W, 3), 40, np.uint8)
small[100:104, 200:206] = (0, 255, 255)   # 24 px < min_area (100 at 1080p scale)
cv2.cvtColor(small, cv2.COLOR_BGR2HSV, dst=tracker_min.hsv)
check("tiny blob below min_area -> no gate", tracker_min.process(0) is None)

# ============ C. serial contract with hysteresis ============
print("=== C. serial: -1,0 once after N empty frames; wrapped angle sends ===")


class StubLink:
    def __init__(self):
        self.lines = []

    def write(self, data):
        self.lines.append(data.decode() if isinstance(data, bytes) else data)
        return True


BLANK = np.full((H, W, 3), 40, np.uint8)   # no gate color anywhere


def feed(tr, frame_bgr, link, now):
    """One frame through process()+handle_serial. frame_bgr=None -> empty scene
    (explicitly blank HSV: the tracker reuses its hsv buffer)."""
    cv2.cvtColor(frame_bgr if frame_bgr is not None else BLANK,
                 cv2.COLOR_BGR2HSV, dst=tr.hsv)
    info = tr.process(0)
    tr.handle_serial(0, info, link, now)
    return info


tr_s = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tr_s.update_gate_from_rgb(0, (255, 255, 0))
link = StubLink()
dt = 1.0 / 26.0
now = 100.0

# gate visible: first frame sends the angle (nothing sent yet)
gate_f = gate_frame([(200, 280), (296, 380)], stray=False)
info = feed(tr_s, gate_f, link, now)
check("first detection -> G1,<angle>,<area> sent",
      link.lines == [f"G1,{info['angle']},{int(info['area'])}\n"], str(link.lines))
now += dt
# same position: no more sends
for _ in range(5):
    feed(tr_s, gate_f, link, now)
    now += dt
check("stable gate -> serial silent", len(link.lines) == 1, str(link.lines))

# gate disappears: grace (4 frames, nothing), then G1,-1,0 EXACTLY ONCE
for k in range(1, 8):
    feed(tr_s, None, link, now)
    now += dt
    sent = [ln for ln in link.lines if ln.startswith("G1,-1")]
    if k < 5:
        check(f"empty frame {k}/N: no loss line yet", not sent, str(link.lines))
    elif k == 5:
        check("5th empty frame (=N): G1,-1,0 sent once",
              sent == ["G1,-1,0\n"], str(link.lines))
    else:
        check(f"empty frame {k}: still exactly one loss line",
              sent == ["G1,-1,0\n"], str(link.lines))
check("total serial lines after loss", link.lines.count("G1,-1,0\n") == 1)

# re-acception: M=3 consecutive frames, then a new angle line
back = gate_frame([(200, 280), (296, 380)], stray=False)
got = None
for k in range(1, 5):
    info = feed(tr_s, back, link, now)
    now += dt
    found_line = [ln for ln in link.lines if ln.startswith("G1,") and "-1" not in ln]
    if k < 3:
        check(f"reappear frame {k}/M: not yet accepted", len(found_line) == 1)
    elif k == 3:
        check("3rd frame (=M): accepted, angle re-sent",
              len(found_line) == 2 and info is not None and not info.get("held"),
              str(link.lines))
        got = info

# wrap-aware threshold: 359 -> 1 (2 deg) must NOT resend; naive diff would.
tr_w = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tr_w.update_gate_from_rgb(0, (255, 255, 0))
link_w = StubLink()
now_w = 200.0
# blob below the +x axis by 1 px -> centroid angle 359; wide blob -> big dx
def blob_at(cy_shift):
    f = np.full((H, W, 3), 40, np.uint8)
    y0 = CY0 + cy_shift
    f[y0 - 10:y0 + 10, 420:460] = (0, 255, 255)   # gate-like 40x20 blob; centroid at y0
    return f

feed(tr_w, blob_at(1), link_w, now_w)          # angle 359
now_w += 0.5                                   # past MIN_SEND_INTERVAL
feed(tr_w, blob_at(-2), link_w, now_w)         # centroid shifted -3 px -> angle 1
angle_lines = [ln for ln in link_w.lines if ln.startswith("G1,") and "-1" not in ln]
check("359 -> 1 is 2 deg (wrapped): no resend",
      len(angle_lines) == 1, str(link_w.lines))
now_w += 0.5
feed(tr_w, blob_at(-6), link_w, now_w)         # -3 px more -> angle 2: diff 3 -> send
angle_lines = [ln for ln in link_w.lines if ln.startswith("G1,") and "-1" not in ln]
check("359 -> 2 is 3 deg: resend", len(angle_lines) == 2, str(link_w.lines))

# MIN_SEND_INTERVAL: >=3 deg change within 0.05 s -> not sent; at 0.10 -> sent
tr_i = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tr_i.update_gate_from_rgb(0, (255, 255, 0))
link_i = StubLink()
now_i = 300.0
feed(tr_i, blob_at(30), link_i, now_i)         # first send
now_i += 0.05
feed(tr_i, blob_at(10), link_i, now_i)         # big jump but too soon
check("MIN_SEND_INTERVAL: too soon -> no line",
      len(link_i.lines) == 1, str(link_i.lines))
now_i += 0.06                                  # now 0.11 since the send
feed(tr_i, blob_at(10), link_i, now_i)
check("MIN_SEND_INTERVAL: after 0.11 s -> sent",
      len(link_i.lines) == 2, str(link_i.lines))

# ============ D. t-shirt does not steal the track ============
print("=== D. t-shirt-like blob does not steal the tracking ===")
tr_t = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tr_t.update_gate_from_rgb(0, (255, 255, 0))
link_t = StubLink()
now_t = 400.0
gate_only = gate_frame([(200, 280), (296, 380)], stray=False)
for _ in range(3):
    feed(tr_t, gate_only, link_t, now_t)
    now_t += dt
base = tr_t.process(0)
base_cx = base["cx"]
# t-shirt appears in another corner, gate still visible
with_shirt = gate_frame([(200, 280), (296, 380)], stray=True)
for _ in range(20):
    info = feed(tr_t, with_shirt, link_t, now_t)
    now_t += dt
check("gate position unchanged while t-shirt visible",
      info is not None and abs(info["cx"] - base_cx) <= 1,
      f"cx={info['cx'] if info else '-'} want {base_cx}")
check("t-shirt never published as the gate",
      info is not None and info["area_px"] < 7000.0,
      f"area_px={info['area_px'] if info else '-'}")

# stable replacement: gate gone, t-shirt persists -> takes over after M
# frames WITHOUT an intermediate loss blip (documented semantics)
tr_r = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1)
tr_r.update_gate_from_rgb(0, (255, 255, 0))
link_r = []
now_r = 500.0
pubs = []
for _ in range(3):
    cv2.cvtColor(gate_only, cv2.COLOR_BGR2HSV, dst=tr_r.hsv)
    pubs.append(tr_r.process(0))
    tr_r.handle_serial(0, pubs[-1], None, now_r)
    now_r += dt
# NOTE: handle_serial(None-link) writes nothing; use process() publications
shirt_only = gate_frame([], stray=True)
for k in range(6):
    cv2.cvtColor(shirt_only, cv2.COLOR_BGR2HSV, dst=tr_r.hsv)
    pubs.append(tr_r.process(0))
    now_r += dt
seq = [(p is not None, p.get("held") if p else None,
        round(p["cx"]) if p else None) for p in pubs[3:]]
print(f"    gate gone, t-shirt persists: {seq}")
check("replacement accepted after M=3 frames, no loss blip before it",
      seq[0][1] is True and seq[1][1] is True
      and seq[2][0] is True and not seq[2][1] and seq[2][2] == 64,
      str(seq))

# ============ E. laptop path (unikostyl.compute_track, SDL dummy) ============
print("=== E. unikostyl.compute_track: same semantics locally ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U

# Idea 8: the laptop self-mask is ON by default now, and this section's
# piece 2 (x 296..380, y 100..140) grazes the center exclusion disk
# (r = 0.22*min(640,360) = 79.2 px) — part of it would be cut out and the
# exact-area assertions below would fail. This suite tests Idea 5
# CLUSTERING alone, so the self-mask is disabled here (restored at the end
# of the section); Idea 8 has its own suite in out/selfmask_check.py.
_self_mask_saved = dict(U.SELF_MASK)
U.set_self_mask_fractions({"enabled": False, "cx": 0.5, "cy": 0.5,
                           "r": 0.22, "r2": 0.0})

for st in U._track_stability:
    st.reset()

# slot 1 = yellow LAB range (not stock)
U.thresholds[0] = [40, 100, -30, 10, 70, 127]  # yellow LAB [97,-22,95]
frame_rgb = np.full((H, W, 3), 40, np.uint8)
frame_rgb[100:140, 200:280] = (255, 255, 0)    # yellow piece 1 (RGB)
frame_rgb[100:140, 296:380] = (255, 255, 0)    # yellow piece 2 (gap 16)
frame_rgb[300:340, 40:90] = (255, 255, 0)      # same-color stray

comb, res, tgt, ang = U.compute_track(U.rgb2lab(frame_rgb))
r0 = next(r for r in res if r["slot"] == 0)
check("laptop: merged gate found", r0["found"])
check("laptop: stray rejected (area = two pieces)",
      abs(r0["area"] - 6560.0) < 1e-6, f"area={r0['area']}")
check("laptop: angle from merged centroid",
      r0["angle"] == GC.angle_deg(r0["cx"], r0["cy"], CX0, CY0),
      f"angle={r0['angle']}")
check("laptop: w_px = cluster width", r0["w_px"] == 180, f"w_px={r0['w_px']}")
check("laptop: radius reported", r0["radius"] > 0, f"radius={r0['radius']:.1f}")
r2 = next(r for r in res if r["slot"] == 2)
check("laptop: unconfigured slot not tracked", not r2["enabled"] and not r2["found"])

# grace + loss on the laptop path too
blank = np.full((H, W, 3), 40, np.uint8)
held_seen = False
lost = False
for _ in range(6):
    comb, res, tgt, ang = U.compute_track(U.rgb2lab(blank))
    r0 = next(r for r in res if r["slot"] == 0)
    if r0.get("held"):
        held_seen = True
    if not r0["found"]:
        lost = True
check("laptop: grace holds the gate (held=True)", held_seen)
check("laptop: loss after N empty frames", lost)

# RING mode: angle from cluster members (area-weighted circular mean)
for st in U._track_stability:
    st.reset()
U.fisheye_mode = 2
try:
    comb, res, tgt, ang = U.compute_track(U.rgb2lab(frame_rgb))
    r0 = next(r for r in res if r["slot"] == 0)
    sin_sum = 0.0
    cos_sum = 0.0
    for x in list(range(200, 280)) + list(range(296, 380)):  # mask columns
        theta = x / W * 2 * math.pi
        sin_sum += math.sin(theta)
        cos_sum += math.cos(theta)
    want = int(-math.degrees(math.atan2(sin_sum, cos_sum))) % 360
    # continuous strip of pieces ~ pixel-sum weighting; centroid-weighted
    # may differ a bit — allow a tolerance band
    check("laptop RING: angle from members (~pixel mean of the strip)",
          min((r0["angle"] - want) % 360, (want - r0["angle"]) % 360) <= 5,
          f"got={r0['angle']} pixel-mean={want}")
finally:
    U.fisheye_mode = 0

# Idea 8: restore the default self-mask state (see the section E preamble).
U.set_self_mask_fractions(_self_mask_saved)

print()
if FAILURES:
    print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("RESULT: all checks passed")
