# -*- coding: utf-8 -*-
"""out/dist_check.py — camera-free verification of Idea 6 (no hardware).

Covers (PROMPT_EN.md §5 Idea 6 + §6):
  A. distance.py units: fit round-trip (synthetic a,b,c -> points -> fit
     recovers them), eval == formula, division guard (r -> b), clamp to
     0..500, NaN/r<=0 junk, r outside the calibration interval, frame
     scaling (Pi 640x360 -> laptop 720x1280 portrait), analytic fallback
     parity with the legacy unikostyl.py formula Z = W*(w/pi)/w_px.
  B. Garbage in -> nothing out: fits on degenerate/increasing data are
     rejected, corrupt/wrong-version/NaN distance_calib.json -> load_model
     returns None (never raises), atomic round-trip save/load exact,
     no *.tmp litter on failure.
  C. Occlusion heuristics + hold (GateDistEstimator): held frame -> hold,
     sudden radius drop on a stationary centroid -> hold, moved centroid
     -> taken at face value, hold expiry after T (honest degradation),
     recovery, low box fill (pieces across the opponent's body), reset.
  D. Pi path (rpi_elp_cam.GateTracker on synthetic frames): dist_cm/
     dist_reliable in the published info (calibrated and fallback models),
     held/unreliable flow through the tracker, build_gate_status adds
     dist_cm/dist_reliable additively (schema Idea 2 intact), --no-dist
     removes them, laptop parse_status_payload passes the fields through.
  E. Laptop path (unikostyl.compute_track under SDL dummy): rec gains
     dist_cm/dist_reliable (fallback formula), calibrated model injected
     -> calibrated values, RING mode honestly reports None.
  F. Calibrator (out/dist_calibrate.py): manual points -> fit -> atomic
     distance_calib.json, --list report, --remove, measured point from a
     synthetic frame matches the tracker's radius semantics.
  G. Python <-> C++ parity: pi_core_tests --dist-parity — same model,
     probes and estimator steps must match bit-for-bit (IEEE-754 through
     %.17g round-trip); the fresh fits agree within tolerance. Skipped
     with a note when the exe was not built.
  H. Cost: one estimator update per gate per frame is microseconds.

Run:  python out\\dist_check.py   (from the repo root or UniKostyl/,
system Python; set PI_CORE_TESTS to point at a custom pi_core_tests exe)
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

import distance as D          # noqa: E402  (Idea 6 pure module)
import gate_cluster as GC     # noqa: E402
import rpi_elp_cam as R       # noqa: E402

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

# Model truth: perspective-like curve d = 4583/(r-1) fits a 45 cm gate at
# f = 640/pi; use a non-trivial (a, b, c) so the fit cannot pass by luck.
TA, TB, TC = -5000.0, 2.0, -1.5


def truth_d(r):
    return TA / (TB - r) - TC


def synth_points():
    rs = [30.0, 45.0, 70.0, 100.0, 140.0]
    return [(truth_d(r), r) for r in rs]


# ============ A. units: fit / eval / guards / scaling / fallback ============
print("=== A. distance.py: fit round-trip, guards, scaling, fallback ===")

fitted, report = D.fit_model(synth_points())
check("fit recovers synthetic (a,b,c)", fitted is not None,
      (report or "").splitlines()[0] if report else "")
if fitted is None:
    print("FATAL: the reference fit failed — the rest of section A is moot")
    sys.exit(1)
check("fit a close to truth", abs(fitted["a"] - TA) <= 1e-6 * abs(TA),
      f"a={fitted['a']!r}")
check("fit b close to truth", abs(fitted["b"] - TB) <= 1e-6,
      f"b={fitted['b']!r}")
check("fit c close to truth", abs(fitted["c"] - TC) <= 1e-6,
      f"c={fitted['c']!r}")

model = D.DistModel(fitted["a"], fitted["b"], fitted["c"], synth_points(),
                     640, 360)
check("model valid", model.valid, model.error)
check("model keeps the calibration r span",
      abs(model.r_lo - 30.0) < 1e-9 and abs(model.r_hi - 140.0) < 1e-9,
      f"[{model.r_lo}, {model.r_hi}]")
d70, ok70 = model.evaluate(70.0, 360)
check("eval == formula (r=70)", ok70 and abs(d70 - truth_d(70.0)) < 1e-9,
      f"d={d70}")
d_far, ok_far = model.evaluate(200.0, 360)
check("r outside calibration interval -> (None, False)",
      d_far is None and not ok_far)
d_pole, ok_pole = model.evaluate(TB, 360)
check("division guard r == b -> (None, False)", d_pole is None and not ok_pole)
d_pole2, ok_pole2 = model.evaluate(TB - 1e-9, 360)
check("division guard r -> b-1e-9 -> unreliable",
      (not ok_pole2) or abs(d_pole2 - D.D_MAX_CM) < 1e-6,
      f"d={d_pole2} ok={ok_pole2}")
d_junk, ok_junk = model.evaluate(float("nan"), 360)
check("NaN r -> (None, False)", d_junk is None and not ok_junk)
d_neg, ok_neg = model.evaluate(-3.0, 360)
check("r <= 0 -> (None, False)", d_neg is None and not ok_neg)
d_big, ok_big = model.evaluate(12.0, 360)
check("d > 500 clamped + unreliable",
      (not ok_big) and d_big is not None and abs(d_big - 500.0) < 1e-9,
      f"d={d_big}")
d_scaled, ok_scaled = model.evaluate(140.0, 720)  # 2x min-dim scale
check("frame scaling: r doubled at 720 min-dim == same distance",
      ok_scaled and abs(d_scaled - truth_d(70.0)) < 1e-9,
      f"d={d_scaled}")
d_scaled2, ok_scaled2 = model.evaluate(70.0, 720)
check("frame scaling: raw r=70 at 720 min-dim reads as r_calib=35",
      ok_scaled2 and abs(d_scaled2 - truth_d(35.0)) < 1e-9,
      f"d={d_scaled2}")

# analytic fallback: the same functional form as the legacy unikostyl.py
# estimate Z = W * (frame_w / pi) / w_px (there: KNOWN_TARGET_WIDTH_CM, here
# the GATE width from the rules — Idea 6 §5 "the specific value is a
# parameter").
for w_px, fw, gw in ((91.7, 640.0, 45.0), (60.0, 640.0, 45.0),
                     (150.0, 1920.0, 60.0), (33.3, 720.0, 50.0)):
    got = D.analytic_distance_cm(w_px, fw, gw)
    want = gw * (fw / math.pi) / w_px
    check(f"analytic fallback parity w={w_px} fw={fw:.0f} gw={gw}",
          abs(got - want) <= 1e-12 * max(1.0, abs(want)), f"{got} vs {want}")
check("analytic fallback rejects junk",
      D.analytic_distance_cm(0, 640, 45) is None
      and D.analytic_distance_cm(50, 0, 45) is None
      and D.analytic_distance_cm(50, 640, 0) is None
      and D.analytic_distance_cm(float("nan"), 640, 45) is None)

# ============ B. garbage models + JSON atomic round-trip ============
print("=== B. distance.py: garbage models, corrupt JSON, atomic write ===")
bad_inc, rep_inc = D.fit_model([(50.0, 30.0), (100.0, 45.0), (150.0, 70.0),
                                (200.0, 100.0)])
check("increasing d(r) fit rejected", bad_inc is None,
      (rep_inc or "").splitlines()[0] if rep_inc else "")
bad_few, _ = D.fit_model([(100.0, 40.0), (90.0, 55.0)])
check("< 3 points rejected", bad_few is None)
bad_same, _ = D.fit_model([(100.0, 40.0), (100.0, 40.0), (100.0, 40.0)])
check("identical points rejected", bad_same is None)
bad_zero, _ = D.fit_model([(100.0, 40.0), (90.0, 0.0), (80.0, 60.0)])
check("r <= 0 point rejected", bad_zero is None)

tmpdir = tempfile.mkdtemp(prefix="dist_check_")
calib_path = os.path.join(tmpdir, "distance_calib.json")
check("missing file -> None", D.load_model(calib_path) is None)
with open(calib_path, "w", encoding="utf-8") as f:
    f.write("{this is not json")
check("corrupt JSON -> None", D.load_model(calib_path) is None)
with open(calib_path, "w", encoding="utf-8") as f:
    json.dump({"version": 9, "a": -1.0, "b": 2.0, "c": 3.0}, f)
check("wrong version -> None", D.load_model(calib_path) is None)
with open(calib_path, "w", encoding="utf-8") as f:
    json.dump({"version": 1, "model": "a/(b-r)-c", "a": 5.0, "b": 2.0,
               "c": 3.0, "points": []}, f)
check("a >= 0 model rejected at load -> None", D.load_model(calib_path) is None)
with open(calib_path, "w", encoding="utf-8") as f:
    json.dump({"version": 1, "model": "a/(b-r)-c", "a": -5.0, "b": 50.0,
               "c": 3.0, "points": [[100, 40], [90, 55], [80, 70]]}, f)
check("asymptote inside the calibration span rejected -> None",
      D.load_model(calib_path) is None)

data = D.model_to_dict(model)
check("schema fields present",
      data["version"] == 1 and data["model"] == "a/(b-r)-c"
      and {"a", "b", "c", "points", "frame_w", "frame_h", "ts"}
      <= set(data.keys()),
      str(sorted(data.keys())))
check("points are [dist_cm, r_px] pairs",
      data["points"] == [[d, r] for d, r in synth_points()])
check("atomic save ok", D.save_model_dict(calib_path, data))
back = D.load_model(calib_path)
check("atomic round-trip exact", back is not None and back.valid
      and back.a == model.a and back.b == model.b and back.c == model.c
      and back.points == model.points)
check("no tmp litter", not [f for f in os.listdir(tmpdir) if f.endswith(".tmp")])

# ============ C. estimator: occlusion heuristics + hold ============
print("=== C. GateDistEstimator: occlusion hold, expiry, recovery ===")
est = D.GateDistEstimator(model=model, hold_s=2.0, drop_frac=0.6,
                          fill_min=0.25, move_frac=0.08,
                          gate_width_cm=45.0)
d, ok = est.update(70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, False, 0.0, 360, 640)
check("clean frame reliable", ok and abs(d - truth_d(70.0)) < 1e-9, f"d={d}")
d, ok = est.update(70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, True, 0.04, 360, 640)
check("held frame: value held, unreliable",
      (not ok) and d is not None and abs(d - truth_d(70.0)) < 1e-9, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 1400.0, 321.0, 181.0, False, 0.08,
                   360, 640)
check("sudden drop, stationary centroid -> unreliable + hold",
      (not ok) and d is not None and abs(d - truth_d(70.0)) < 1e-9, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, False, 0.12,
                   360, 640)
check("moved centroid + drop = gates moved (not occlusion)",
      ok and abs(d - truth_d(35.0)) < 1e-9, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, True, 2.0, 360, 640)
check("held within T after the reliable frame",
      (not ok) and d is not None and abs(d - truth_d(35.0)) < 1e-9, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, True, 2.2, 360, 640)
check("hold expires after T -> nothing reported",
      (not ok) and d is None, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, False, 2.4, 360, 640)
check("recovery reliable again",
      ok and abs(d - truth_d(35.0)) < 1e-9, f"d={d}")
d, ok = est.update(35.0, 45.0, 40.0, 400.0, 420.0, 200.0, False, 2.44, 360, 640)
check("low box fill (pieces across the body) -> unreliable + hold",
      (not ok) and d is not None and abs(d - truth_d(35.0)) < 1e-9, f"d={d}")
d, ok = est.update(float("nan"), 45.0, 40.0, 400.0, 420.0, 200.0, False, 2.5,
                   360, 640)
check("junk radius inside the hold window -> held value",
      (not ok) and d is not None and abs(d - truth_d(35.0)) < 1e-9, f"d={d}")
est.reset()
d, ok = est.update(35.0, 45.0, 40.0, 400.0, 420.0, 200.0, False, 4.0, 360, 640)
check("reset clears the hold", (not ok) and d is None, f"d={d}")
est2 = D.GateDistEstimator(model=None, gate_width_cm=45.0)
d, ok = est2.update(70.0, 91.7, 40.0, 2800.0, 320.0, 180.0, False, 0.0, 360, 640)
check("no calibration -> analytic fallback drives the estimator",
      ok and abs(d - D.analytic_distance_cm(91.7, 640.0, 45.0)) < 1e-9,
      f"d={d}")
d, ok = est2.update(float("nan"), 91.7, 40.0, 2800.0, 320.0, 180.0, False,
                    0.04, 360, 640)
check("junk radius holds within T (est2 has a hold from frame 0)",
      (not ok) and d is not None
      and abs(d - D.analytic_distance_cm(91.7, 640.0, 45.0)) < 1e-9, f"d={d}")
est3 = D.GateDistEstimator(model=None, gate_width_cm=45.0)
d, ok = est3.update(float("nan"), 91.7, 40.0, 2800.0, 320.0, 180.0, False,
                    0.04, 360, 640)
check("NaN radius with no hold -> nothing reported",
      (not ok) and d is None, f"d={d}")

# ============ D. Pi path (GateTracker + build_gate_status) ============
print("=== D. rpi_elp_cam: tracker dist fields + status JSON additive ===")


def bgr_frame(blobs, bg=40, blob_bgr=(0, 255, 255)):  # pure yellow in BGR
    f = np.full((H, W, 3), bg, np.uint8)
    for (x0, x1, y0, y1) in blobs:
        f[y0:y1, x0:x1] = blob_bgr
    return f


def make_tracker(**kwargs):
    tr = R.GateTracker(W, H, (40, 100, 100), 100.0, 1.0, 1, **kwargs)
    tr.update_gate_from_rgb(0, (255, 255, 0))
    return tr


blob = [(400, 460, 100, 140)]          # 60x40 -> radius 36.06, w 60
tr = make_tracker()
cv2.cvtColor(bgr_frame(blob), cv2.COLOR_BGR2HSV, dst=tr.hsv)
info = tr.process(0)
check("tracker: gate found on the first frame", info is not None)
check("tracker: fallback dist_cm present and reliable",
      info is not None and info.get("dist_cm") is not None
      and info.get("dist_reliable") is True,
      f"dist={info.get('dist_cm') if info else '-'}")
check("tracker: fallback dist == W*(w/pi)/w_px",
      info is not None and abs(info["dist_cm"]
                               - D.analytic_distance_cm(60.0, 640.0, 45.0))
      < 1e-9,
      f"dist={info.get('dist_cm') if info else '-'} vs "
      f"{D.analytic_distance_cm(60.0, 640.0, 45.0)}")

# calibrated model: swap it in, the same frame gives the calibrated value
tr.set_distance_model(model)
cv2.cvtColor(bgr_frame(blob), cv2.COLOR_BGR2HSV, dst=tr.hsv)
info_cal = tr.process(0)
check("tracker: calibrated dist_cm",
      info_cal is not None
      and abs(info_cal["dist_cm"] - truth_d(info_cal["radius"])) < 1e-9,
      f"dist={info_cal.get('dist_cm') if info_cal else '-'} "
      f"radius={info_cal.get('radius') if info_cal else '-'}")

# occlusion inside the tracker: shrink the blob keeping the centroid
tr2 = make_tracker()
tr2.set_distance_model(model)
cv2.cvtColor(bgr_frame(blob), cv2.COLOR_BGR2HSV, dst=tr2.hsv)
info_a = tr2.process(0)
small = [(415, 445, 110, 130)]         # 30x20 at the same centroid, r ~ 18
cv2.cvtColor(bgr_frame(small), cv2.COLOR_BGR2HSV, dst=tr2.hsv)
info_b = tr2.process(0)
check("tracker: occluded gate -> dist_reliable False, value held",
      info_b is not None and info_b.get("dist_reliable") is False
      and info_b.get("dist_cm") is not None
      and abs(info_b["dist_cm"] - info_a["dist_cm"]) < 1e-9,
      f"a={info_a.get('dist_cm') if info_a else '-'} "
      f"b={info_b.get('dist_cm') if info_b else '-'}")

# gate lost: published None after the hysteresis, status has no dist keys
cv2.cvtColor(bgr_frame([]), cv2.COLOR_BGR2HSV, dst=tr2.hsv)
info_lost = None
for _ in range(6):
    info_lost = tr2.process(0)
check("tracker: gate lost after the hysteresis -> no dist fields",
      info_lost is None)

# --no-dist: no dist fields at all
tr3 = make_tracker(dist_enabled=False)
cv2.cvtColor(bgr_frame(blob), cv2.COLOR_BGR2HSV, dst=tr3.hsv)
info_nd = tr3.process(0)
check("tracker: --no-dist -> no dist_cm in info",
      info_nd is not None and "dist_cm" not in info_nd
      and "dist_reliable" not in info_nd)

# build_gate_status: additive fields, old keys untouched
tr4 = make_tracker()
cv2.cvtColor(bgr_frame(blob), cv2.COLOR_BGR2HSV, dst=tr4.hsv)
info4 = tr4.process(0)
status = R.build_gate_status(tr4, {0: info4}, 25.0)
g1 = status["gates"]["1"]
check("status: found/enabled/angle/area/radius intact",
      g1.get("found") is True and g1.get("enabled") is True
      and "angle" in g1 and "area" in g1 and "radius" in g1,
      str(sorted(g1.keys())))
check("status: dist_cm + dist_reliable additive",
      isinstance(g1.get("dist_cm"), float)
      and g1.get("dist_reliable") is True,
      f"dist_cm={g1.get('dist_cm')}")
status_lost = R.build_gate_status(tr4, {0: None}, 25.0)
g1_lost = status_lost["gates"]["1"]
check("status: lost gate -> no dist_cm/dist_reliable",
      "dist_cm" not in g1_lost and "dist_reliable" not in g1_lost,
      str(sorted(g1_lost.keys())))
status_held = R.build_gate_status(tr4, {0: dict(info4, held=True,
                                                dist_reliable=False)}, 25.0)
g1_held = status_held["gates"]["1"]
check("status: held/unreliable dist keeps dist_cm with reliable=false",
      g1_held.get("dist_cm") is not None
      and g1_held.get("dist_reliable") is False
      and g1_held.get("held") is True)

# laptop parser passes the fields through (status_panel, SDL dummy)
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
import status_panel as SP  # noqa: E402
parsed = SP.parse_status_payload(json.dumps(status))
check("parse_status_payload keeps dist_cm/dist_reliable",
      parsed is not None
      and parsed["gates"]["1"].get("dist_cm") == g1.get("dist_cm")
      and parsed["gates"]["1"].get("dist_reliable") is True)
parsed_lost = SP.parse_status_payload(json.dumps(status_lost))
check("parse_status_payload: lost gate without dist",
      parsed_lost is not None and "dist_cm" not in parsed_lost["gates"]["1"])

# ============ E. laptop path (unikostyl.compute_track, SDL dummy) ============
print("=== E. unikostyl.compute_track: rec gains dist_cm/dist_reliable ===")
os.environ["SDL_VIDEODRIVER"] = "dummy"
import unikostyl as U  # noqa: E402


def lab_range_for(frame_rgb, x0, x1, y0, y1, pad=25):
    lab = U.rgb2lab(frame_rgb)[y0:y1, x0:x1].reshape(-1, 3)
    return [int(lab[:, 0].min()) - pad, int(lab[:, 0].max()) + pad,
            int(lab[:, 1].min()) - pad, int(lab[:, 1].max()) + pad,
            int(lab[:, 2].min()) - pad, int(lab[:, 2].max()) + pad]


U.thresholds[0] = [0, 100, -128, 127, -128, 127]
frame_rgb = np.full((H, W, 3), 40, np.uint8)
frame_rgb[100:140, 400:460] = (255, 255, 0)     # the same yellow blob
U.thresholds[0] = lab_range_for(frame_rgb, 400, 460, 100, 140)
for st in U._track_stability:
    st.reset()
U._dist_est = None                              # fresh estimators (no calib)
comb, res, tgt, ang = U.compute_track(U.rgb2lab(frame_rgb))
r0 = next(r for r in res if r["slot"] == 0)
check("laptop rec: dist_cm present with the fallback formula",
      r0["found"] and r0.get("dist_cm") is not None
      and r0.get("dist_reliable") is True
      and abs(r0["dist_cm"] - D.analytic_distance_cm(r0["w_px"], 640.0,
                                                     U.GATE_WIDTH_CM)) < 1e-9,
      f"dist={r0.get('dist_cm')} w_px={r0['w_px']}")
check("laptop rec: dist_reliable key always present",
      "dist_reliable" in r0)

# calibrated model injected: the readout follows the calibration
U._DIST_MODEL = model
U._dist_est = None
for st in U._track_stability:
    st.reset()
comb, res, tgt, ang = U.compute_track(U.rgb2lab(frame_rgb))
r0c = next(r for r in res if r["slot"] == 0)
check("laptop rec: calibrated dist_cm",
      r0c["found"] and abs(r0c["dist_cm"] - truth_d(r0c["radius"])) < 1e-9,
      f"dist={r0c.get('dist_cm')} radius={r0c['radius']}")
U._DIST_MODEL = None
U._dist_est = None

# RING mode: strip geometry, distance honestly not computed
U.fisheye_mode = 2
try:
    for st in U._track_stability:
        st.reset()
    comb5, res5, tgt5, ang5 = U.compute_track(U.rgb2lab(frame_rgb))
    r0r = next(r for r in res5 if r["slot"] == 0)
    check("laptop RING: dist_cm None (strip sizes are not physical)",
          r0r.get("dist_cm") is None and r0r.get("dist_reliable") is False,
          f"dist={r0r.get('dist_cm')}")
finally:
    U.fisheye_mode = 0
for st in U._track_stability:
    st.reset()

# ============ F. calibrator (out/dist_calibrate.py) ============
print("=== F. dist_calibrate.py: manual points, fit, report, image ===")
# Отдельная подпапка: секция B уже писала tmpdir/distance_calib.json —
# калибратор подхватывает существующий файл как набор точек (фича), для
# чистоты сценария F начинает с пустой папки.
fdir = os.path.join(tmpdir, "calib_f")
os.makedirs(fdir, exist_ok=True)
cal_out = os.path.join(fdir, "distance_calib.json")
cal_sess = os.path.join(fdir, "session.json")


def run_cal(*extra):
    return subprocess.run(
        [sys.executable, os.path.join(HERE, "dist_calibrate.py"),
         "--out", cal_out, "--session", cal_sess, *extra],
        capture_output=True, text=True, timeout=120,
        cwd=os.path.join(HERE, ".."))


def cal_points():
    m = D.load_model(cal_out)
    return m.points if m is not None else []


def sess_points():
    try:
        with open(cal_sess, "r", encoding="utf-8") as f:
            return len(json.load(f).get("points", []))
    except Exception:
        return -1


# truth-derived pairs (dist from the same synthetic model as section A)
truth_pts = [(truth_d(r), r) for r in (30.0, 45.0, 70.0, 100.0, 140.0)]
add_args = []
for dd, rr in truth_pts:
    add_args += ["--add", f"{dd:.10f},{rr:.10f}"]
res_add = run_cal(*add_args)
check("calibrator: --add exit 0", res_add.returncode == 0,
      res_add.stderr[-200:] if res_add.returncode else "")
check("calibrator: distance_calib.json written and valid",
      os.path.exists(cal_out))
cal_model = D.load_model(cal_out)
check("calibrator: model loads and fits the truth",
      cal_model is not None and cal_model.valid
      and abs(cal_model.a - TA) <= 1e-4 * abs(TA),
      f"a={getattr(cal_model, 'a', None)}")
check("calibrator: frame size recorded", cal_model is not None
      and cal_model.frame_w == 640 and cal_model.frame_h == 360)
res_list = run_cal("--list")
check("calibrator: --list prints residuals",
      res_list.returncode == 0 and "невязка" in res_list.stdout)
res_rm = run_cal("--remove", "1")
check("calibrator: --remove drops a point (5 -> 4)",
      res_rm.returncode == 0 and len(cal_points()) == 4,
      f"points={len(cal_points())}")
# down to 1 point: fit must fail, session keeps the points, calib file
# keeps the last valid model (4 points)
res_few = run_cal("--remove", "1", "--remove", "2", "--remove", "3")
check("calibrator: too few points -> fit refused, calib file untouched",
      res_few.returncode == 0 and len(cal_points()) == 4
      and sess_points() == 1,
      f"file points={len(cal_points())}, session={sess_points()}")
# back to 3 points via manual adds -> fit works again
res_add2 = run_cal("--add", f"{truth_d(60.0):.10f},60.0",
                   "--add", f"{truth_d(90.0):.10f},90.0")
check("calibrator: --add restores a valid fit",
      res_add2.returncode == 0 and len(cal_points()) == 3
      and D.load_model(cal_out).valid)

# measured point from a synthetic frame: same radius semantics as the tracker
img_path = os.path.join(tmpdir, "frame.png")
cv2.imwrite(img_path, bgr_frame([(400, 460, 100, 140)]))
res_img = run_cal("--image", img_path, "--dist", "100", "--rgb", "255,255,0")
check("calibrator: --image measures the blob", res_img.returncode == 0,
      res_img.stdout[-200:] if res_img.returncode else res_img.stderr[-200:])
cal_model2 = D.load_model(cal_out)
measured = [r for d, r in cal_model2.points] if cal_model2 else []
expect_r = 0.5 * math.sqrt(60.0 ** 2 + 40.0 ** 2)   # union-box circumcircle
check("calibrator: measured r == tracker radius semantics (36.06 px)",
      any(abs(r - expect_r) < 0.5 for r in measured),
      f"radii={measured}")

# ============ G. Python <-> C++ parity ============
print("=== G. pi_core parity: --dist-parity bit-for-bit ===")
tests_exe = os.environ.get(
    "PI_CORE_TESTS",
    os.path.join(HERE, "pi_core_build_win", "pi_core_tests.exe"))
if not os.path.exists(tests_exe):
    skip("C++ parity", f"pi_core_tests not found at {tests_exe} "
                       "(rebuild with python -m ziglang c++ -O2)")
else:
    par_path = os.path.join(tmpdir, "dist_parity.txt")
    lines = []
    py_fit, _ = D.fit_model(synth_points())
    pts = synth_points()
    # float() — repr(np.float64) не парсится C++ fscanf (numpy 2.x).
    lines.append("MODEL {!r} {!r} {!r} 640 360".format(
        float(py_fit["a"]), float(py_fit["b"]), float(py_fit["c"])))
    lines.append(f"PTS {len(pts)}")
    for dd, rr in pts:
        lines.append(f"{float(dd)!r} {float(rr)!r}")
    lines.append(f"FIT {len(pts)}")
    for dd, rr in pts:
        lines.append(f"{float(dd)!r} {float(rr)!r}")
    probes = [(360, 70.0), (360, 30.0), (360, 140.0), (720, 140.0),
              (360, 12.0), (360, TB), (360, 200.0), (360, float("nan"))]
    for min_dim, rr in probes:
        lines.append(f"EVAL {min_dim} {rr!r}")
    est_args = (2.0, 0.6, 0.25, 0.08, 45.0)
    lines.append("EST {!r} {!r} {!r} {!r} {!r}".format(*est_args))
    steps = [
        (70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, 0, 0.0, 360, 640),
        (70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, 1, 0.04, 360, 640),
        (35.0, 45.0, 40.0, 1400.0, 321.0, 181.0, 0, 0.08, 360, 640),
        (35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, 0, 0.12, 360, 640),
        (35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, 1, 2.0, 360, 640),
        (35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, 1, 2.2, 360, 640),
        (35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, 0, 2.4, 360, 640),
        (35.0, 45.0, 40.0, 400.0, 420.0, 200.0, 0, 2.44, 360, 640),
        (float("nan"), 45.0, 40.0, 400.0, 420.0, 200.0, 0, 2.5, 360, 640),
    ]
    for s in steps:
        lines.append("STEP {!r} {!r} {!r} {!r} {!r} {!r} {!r} {!r} {!r} {!r}"
                     .format(*s))
    with open(par_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    res = subprocess.run([tests_exe, "--dist-parity", par_path],
                         capture_output=True, text=True, timeout=120)
    check("pi_core_tests --dist-parity ran clean", res.returncode == 0,
          res.stdout[-300:] if res.returncode else "")
    out_lines = [ln for ln in res.stdout.splitlines()
                 if ln.startswith(("MODEL ", "FITTED ", "EVAL ", "STEP "))]

    # MODEL: same a,b,c, same validity (with the calibration points — both
    # sides build the identical r-interval)
    m_line = next((ln for ln in out_lines if ln.startswith("MODEL ")), None)
    cpp = m_line.split() if m_line else []
    py_model_par = D.DistModel(float(py_fit["a"]), float(py_fit["b"]),
                               float(py_fit["c"]), pts, 640, 360)
    check("parity MODEL: a/b/c identical through the round-trip",
          len(cpp) == 5 and float(cpp[1]) == py_fit["a"]
          and float(cpp[2]) == py_fit["b"] and float(cpp[3]) == py_fit["c"],
          f"c++={m_line}")
    check("parity MODEL: validity matches",
          len(cpp) == 5 and (cpp[4] == "1") == py_model_par.valid)

    # FITTED: both solvers agree (tolerance), same validity
    f_line = next((ln for ln in out_lines if ln.startswith("FITTED ")), None)
    cf = f_line.split() if f_line else []
    cpp_fitted_valid = len(cf) == 5 and cf[4] == "1"
    check("parity FITTED: C++ fit valid",
          cpp_fitted_valid, f_line or "")
    if cpp_fitted_valid:
        ca, cb, cc = float(cf[1]), float(cf[2]), float(cf[3])
        check("parity FITTED: params agree with the Python fit",
              abs(ca - py_fit["a"]) <= 1e-4 * abs(py_fit["a"])
              and abs(cb - py_fit["b"]) <= 1e-4 * max(1.0, abs(py_fit["b"]))
              and abs(cc - py_fit["c"]) <= 1e-4 * max(1.0, abs(py_fit["c"])),
              f"c++=({ca}, {cb}, {cc}) py=({py_fit['a']}, {py_fit['b']}, "
              f"{py_fit['c']})")
        cpp_model = D.DistModel(ca, cb, cc, synth_points(), 640, 360)
        agree = all(
            cpp_model.valid and py_model_par.evaluate(rr, 360)[0] is not None
            and abs(cpp_model.evaluate(rr, 360)[0]
                    - py_model_par.evaluate(rr, 360)[0])
            <= 1e-3 * abs(py_model_par.evaluate(rr, 360)[0])
            for _, rr in [(0, 70.0), (0, 45.0), (0, 100.0)])
        check("parity FITTED: curves agree within 0.1% on the probes",
              agree)

    # EVAL: bit-for-bit against Python eval of the same model
    eval_lines = [ln for ln in out_lines if ln.startswith("EVAL ")]
    check("parity EVAL: line count", len(eval_lines) == len(probes),
          f"{len(eval_lines)} vs {len(probes)}")
    eval_bad = []
    for (min_dim, rr), ln in zip(probes, eval_lines):
        parts = ln.split()
        cpp_d = float(parts[1])
        cpp_ok = parts[2] == "1"
        py_d, py_ok = py_model_par.evaluate(rr, min_dim)
        py_d = float("nan") if py_d is None else py_d
        same = (py_ok == cpp_ok) and (
            (math.isnan(py_d) and math.isnan(cpp_d))
            or py_d == cpp_d)
        if not same:
            eval_bad.append(f"r={rr}: py=({py_d}, {py_ok}) c++={ln}")
    check("parity EVAL: bit-for-bit on all probes", not eval_bad,
          "; ".join(eval_bad[:3]))

    # EST/STEP: bit-for-bit estimator parity
    step_lines = [ln for ln in out_lines if ln.startswith("STEP ")]
    py_est = D.GateDistEstimator(model=py_model_par, hold_s=est_args[0],
                                 drop_frac=est_args[1], fill_min=est_args[2],
                                 move_frac=est_args[3],
                                 gate_width_cm=est_args[4])
    step_bad = []
    for s, ln in zip(steps, step_lines):
        parts = ln.split()
        cpp_d = float(parts[1])
        cpp_ok = parts[2] == "1"
        py_d, py_ok = py_est.update(s[0], s[1], s[2], s[3], s[4], s[5],
                                    bool(s[6]), s[7], s[8], s[9])
        py_d = float("nan") if py_d is None else py_d
        same = (py_ok == cpp_ok) and (
            (math.isnan(py_d) and math.isnan(cpp_d)) or py_d == cpp_d)
        if not same:
            step_bad.append(f"step={s[:4]}...: py=({py_d}, {py_ok}) c++={ln}")
    check("parity STEP: estimator bit-for-bit on all steps",
          len(step_lines) == len(steps) and not step_bad,
          "; ".join(step_bad[:3]) if step_bad
          else f"{len(step_lines)} steps")

# ============ H. cost + CLI surface ============
print("=== H. cost + flags ===")
t0 = time.perf_counter()
n = 2000
for i in range(n):
    est.update(70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, False, i * 0.001, 360,
               640)
dt_ms = (time.perf_counter() - t0) / n * 1000.0
check("estimator update cost negligible (< 0.02 ms/gate)", dt_ms < 0.02,
      f"{dt_ms:.4f} ms per call")

res_help = subprocess.run(
    [sys.executable, os.path.join(HERE, "..", "rpi_elp_cam.py"), "--help"],
    capture_output=True, text=True, timeout=60,
    cwd=os.path.join(HERE, ".."))
for flag in ("--dist-calib", "--dist-hold", "--dist-drop", "--dist-fill",
             "--gate-width", "--no-dist"):
    check(f"rpi_elp_cam --help mentions {flag}", flag in res_help.stdout)

res_cal_help = subprocess.run(
    [sys.executable, os.path.join(HERE, "dist_calibrate.py"), "--help"],
    capture_output=True, text=True, timeout=60)
check("dist_calibrate --help ok", res_cal_help.returncode == 0)

print()
print(f"TOTAL: {len(FAILURES)} failed, {len(SKIPPED)} skipped")
if FAILURES:
    print("FAILED checks:")
    for name in FAILURES:
        print(f"  - {name}")
if SKIPPED:
    for name in SKIPPED:
        print(f"  (skipped) {name}")
sys.exit(1 if FAILURES else 0)
