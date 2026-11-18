# -*- coding: utf-8 -*-
"""out/parity_check5.py — Idea 5 parity: Python gate_cluster vs C++ pi_core.

Generates a deterministic (seeded) scenario of synthetic frames, extracts
REAL connected-components statistics from masks (cv2, exactly like
rpi_elp_cam.py detect_gate), quantizes them to the serialized precision,
computes the Python results (gate_cluster.build_gate_object +
GateStability) and feeds THE SAME component vectors to the C++ pure logic
(pi_core_tests --cluster-parity FILE). Every published frame must match.

Phases:
  1. gate pieces with a gap + speckles + far t-shirt, 1-px jitter
  2. gate disappears (empty frames -> grace/loss)
  3. gate reappears with 1-frame flickers
  4. t-shirt appears while the gate is visible (must not steal)
  5. gate gone, t-shirt persists (stable replacement after M frames)
  6. speckle storm (> 256 components -> candidate cap)
  7. gaps exactly at the merge boundary
  8. slowly drifting cluster

Run:  python out\\parity_check5.py   (from UniKostyl/, system Python;
      pi_core_tests must be available — pass --tests <path> to override)
"""
import argparse
import os
import subprocess
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import gate_cluster as GC

H, W = 360, 640
FAILURES = []


def check(name, cond, detail=""):
    print(f"[{'PASS' if cond else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILURES.append(name)


def quant(v):
    """The value as it will travel through the vectors file (%.3f -> strtod):
    quantize BEFORE computing so both sides see identical doubles."""
    return float(f"{v:.3f}")


def comps_from_mask(mask, min_area):
    """CC statistics exactly like gate_cluster.detect_gate (before
    build_gate_object), quantized to the file precision."""
    num, _lab, stats, cents = cv2.connectedComponentsWithStats(
        mask, connectivity=8, ltype=cv2.CV_32S)
    comps = []
    for i in range(1, num):
        a = float(stats[i, cv2.CC_STAT_AREA])
        if a < min_area:
            continue
        comps.append((
            quant(float(cents[i, 0])),
            quant(float(cents[i, 1])),
            quant(a),
            quant(float(stats[i, cv2.CC_STAT_LEFT])),
            quant(float(stats[i, cv2.CC_STAT_TOP])),
            quant(float(stats[i, cv2.CC_STAT_WIDTH])),
            quant(float(stats[i, cv2.CC_STAT_HEIGHT])),
        ))
    return comps


def draw_scene(rng, pieces, shirt=None, speckles=0):
    mask = np.zeros((H, W), np.uint8)
    for (cx, cy, hw, hh) in pieces:
        x0 = int(round(cx - hw))
        y0 = int(round(cy - hh))
        mask[y0:y0 + 2 * hh, x0:x0 + 2 * hw] = 255
    if shirt is not None:
        cx, cy, hw, hh = shirt
        x0 = int(round(cx - hw))
        y0 = int(round(cy - hh))
        mask[y0:y0 + 2 * hh, x0:x0 + 2 * hw] = 255
    for _ in range(speckles):
        x = int(rng.integers(0, W - 4))
        y = int(rng.integers(0, H - 4))
        mask[y:y + 2, x:x + 2] = 255
    return mask


def build_scenario(rng):
    """Returns a list of frames; each frame is a list of components (a gate
    at a different position each time or an empty mask)."""
    frames = []
    min_area = 100.0
    gap = 16  # merge_gap at 640 wide

    # Phase 1: gate from two pieces (16 px gap) + speckles + far shirt.
    for k in range(12):
        dx = int(rng.integers(-1, 2))
        dy = int(rng.integers(-1, 2))
        mask = draw_scene(
            rng,
            [(320 + dx, 120 + dy, 40, 20), (376 + gap + dx, 120 + dy, 42, 20)],
            shirt=(60, 320, 25, 20),
            speckles=6,
        )
        frames.append(comps_from_mask(mask, min_area))

    # Phase 2: gate disappears (grace -> loss).
    for _ in range(7):
        frames.append([])

    # Phase 3: gate reappears with 1-frame flickers.
    for k in range(9):
        if k % 2 == 0:
            mask = draw_scene(rng, [(320, 120, 40, 20), (376 + gap, 120, 42, 20)])
            frames.append(comps_from_mask(mask, min_area))
        else:
            frames.append([])

    # Phase 4: t-shirt appears nearer to the gate than before; gate visible
    # (anchor preference keeps the gate).
    for k in range(10):
        mask = draw_scene(
            rng,
            [(320, 120, 40, 20), (376 + gap, 120, 42, 20)],
            shirt=(360, 300, 30, 25),
        )
        frames.append(comps_from_mask(mask, min_area))

    # Phase 5: gate gone, shirt persists (stable replacement after M frames).
    for k in range(8):
        mask = draw_scene(rng, [], shirt=(360, 300, 30, 25))
        frames.append(comps_from_mask(mask, min_area))

    # Phase 6: speckle storm -> candidate cap.
    mask = np.zeros((H, W), np.uint8)
    for _ in range(400):
        x = int(rng.integers(0, W - 6))
        y = int(rng.integers(0, H - 6))
        mask[y:y + 2, x:x + 2] = 255
    frames.append(comps_from_mask(mask, min_area))
    frames.append([])

    # Phase 7: gaps exactly at the merge boundary (merge if <= merge_gap).
    for g in (15, 16, 17):
        mask = np.zeros((H, W), np.uint8)
        mask[100:140, 200:280] = 255
        mask[100:140, 280 + g:280 + g + 60] = 255
        frames.append(comps_from_mask(mask, min_area))

    # Phase 8: slow drift (a few px per frame — continuity radius keeps it).
    for k in range(10):
        mask = draw_scene(rng, [(320 + k * 5, 120 + k * 2, 40, 20),
                                (376 + gap + k * 5, 120 + k * 2, 42, 20)])
        frames.append(comps_from_mask(mask, min_area))

    return frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tests", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "pi_core_build_win", "pi_core_tests.exe"),
        help="pi_core_tests binary (built with zig c++ on Windows)")
    ap.add_argument("--vectors", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "parity5_vectors.txt"))
    ap.add_argument("--seed", type=int, default=20261117)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    params = GC.resolve_params(W, H, min_area_px=100.0)
    frames = build_scenario(rng)
    print(f"scenario: {len(frames)} frames, params "
          f"merge_gap={params['merge_gap_px']} min_area={params['min_area_px']} "
          f"max_stray={params['max_stray_px']} lost={params['lost_frames']} "
          f"confirm={params['confirm_frames']}")

    # ---- Python reference (on the SAME quantized components) ----
    st = GC.GateStability(params["lost_frames"], params["confirm_frames"],
                          params["max_stray_px"])
    py_lines = []
    n_comp_total = 0
    for comps in frames:
        n_comp_total += len(comps)
        prev = st.held_xy()
        det = GC.build_gate_object(
            comps, params, prev[0] if prev else None,
            prev[1] if prev else None)
        pub = st.update(det)
        if pub is None:
            py_lines.append("R 0 0 - - - - -")
        else:
            py_lines.append(
                "R 1 {} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f}".format(
                    1 if pub.get("held") else 0,
                    pub["cx"], pub["cy"], pub["area"], pub["radius"],
                    pub["w"]))
    print(f"python: {n_comp_total} components total, {len(py_lines)} frames")

    # ---- vectors file ----
    with open(args.vectors, "w", encoding="utf-8") as f:
        f.write("P {} {} {} {} {}\n".format(
            params["merge_gap_px"], params["min_area_px"],
            params["max_stray_px"], params["lost_frames"],
            params["confirm_frames"]))
        for comps in frames:
            f.write(f"F {len(comps)}\n")
            for c in comps:
                f.write("C {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f}\n".format(*c))

    # ---- C++ run ----
    if not os.path.exists(args.tests):
        print(f"FAIL: pi_core_tests not found at {args.tests} "
              f"(build with: python -m ziglang c++ -O2 ...)")
        sys.exit(1)
    res = subprocess.run([args.tests, "--cluster-parity", args.vectors],
                         capture_output=True, text=True, timeout=120)
    cpp_lines = [ln for ln in res.stdout.splitlines()
                 if ln.startswith("R ") or ln.startswith("FAIL")
                 or ln.startswith("PARITY-FRAMES")]
    print(f"c++: rc={res.returncode}, {len(cpp_lines)} lines")
    tail = [ln for ln in cpp_lines if not ln.startswith("R ")]
    frames_line = next((ln for ln in tail if ln.startswith("PARITY-FRAMES")), None)
    check("C++ parsed all frames",
          frames_line == f"PARITY-FRAMES {len(frames)}", str(tail))
    check("C++ reported no failures",
          not any(ln.startswith("FAIL") for ln in cpp_lines))

    cpp_r = [ln for ln in cpp_lines if ln.startswith("R ")]
    check("same number of R lines", len(cpp_r) == len(py_lines),
          f"py={len(py_lines)} cpp={len(cpp_r)}")

    mismatches = []
    for i, (a, b) in enumerate(zip(py_lines, cpp_r)):
        if a != b:
            mismatches.append((i, a, b))
    check("all frames match bit-for-bit (format %.3f)",
          not mismatches,
          "" if not mismatches else f"first: frame {mismatches[0][0]} "
          f"py={mismatches[0][1]} cpp={mismatches[0][2]}")
    for i, a, b in mismatches[:5]:
        print(f"    frame {i}: py={a}  cpp={b}")

    # Sanity: the scenario actually exercised all the interesting paths.
    found_cnt = sum(1 for ln in py_lines if ln.startswith("R 1 0 "))
    held_cnt = sum(1 for ln in py_lines if ln.startswith("R 1 1 "))
    lost_cnt = sum(1 for ln in py_lines if ln.startswith("R 0 "))
    print(f"coverage: found={found_cnt} held(grace)={held_cnt} lost={lost_cnt}")
    check("scenario covers found/held/lost states",
          found_cnt > 10 and held_cnt > 2 and lost_cnt > 2)

    print()
    if FAILURES:
        print(f"RESULT: {len(FAILURES)} FAILURES: {FAILURES}")
        sys.exit(1)
    print("RESULT: parity OK — Python gate_cluster == C++ pi_core gate_cluster")


if __name__ == "__main__":
    main()
