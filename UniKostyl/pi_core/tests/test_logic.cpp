// pi_core/tests/test_logic.cpp — unit tests for the pure-logic layer.
// Builds WITHOUT OpenCV/ZMQ (only nlohmann/json + STL), so it can run on any
// machine: ./pi_core_tests [--vectors FILE]
//   --vectors FILE: lines "r g b h s v" produced by tests/parity_hsv.py —
//   parity check of the C++ rgb_to_hsv_8u against the machine's real cv2
//   (run it on the Pi against its OpenCV 4.10).
#include "hsv_color.hpp"
#include "gate_logic.hpp"
#include "gate_cluster.hpp"
#include "self_mask.hpp"
#include "distance.hpp"
#include "gate_store.hpp"
#include "color_message.hpp"
#include "atomic_file.hpp"

#include "hsv_golden.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace pi_core;
using namespace pi_core_tests;  // kHsvGolden / kHsvGoldenCount

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s %s\n", what, detail.c_str());
    }
}

void check_eq(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s got=%d want=%d\n", what, got, want);
    }
}

void check_str(const std::string& got, const std::string& want,
               const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s got=\"%s\" want=\"%s\"\n", what, got.c_str(),
                    want.c_str());
    }
}

// ---------------- HSV conversion ----------------

void test_hsv_golden() {
    for (size_t i = 0; i < kHsvGoldenCount; ++i) {
        const Hsv px = rgb_to_hsv_8u(kHsvGolden[i].rgb[0], kHsvGolden[i].rgb[1],
                                     kHsvGolden[i].rgb[2]);
        char what[96];
        std::snprintf(what, sizeof(what), "hsv_golden[%zu] rgb=%d,%d,%d", i,
                      kHsvGolden[i].rgb[0], kHsvGolden[i].rgb[1],
                      kHsvGolden[i].rgb[2]);
        ++g_checks;
        if (px.h != kHsvGolden[i].hsv[0] || px.s != kHsvGolden[i].hsv[1] ||
            px.v != kHsvGolden[i].hsv[2]) {
            ++g_failures;
            std::printf("FAIL: %s got=%d,%d,%d want=%d,%d,%d\n", what, px.h,
                        px.s, px.v, kHsvGolden[i].hsv[0],
                        kHsvGolden[i].hsv[1], kHsvGolden[i].hsv[2]);
        }
    }
}

void test_hsv_vectors_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("FAIL: cannot open vectors file %s\n", path);
        ++g_failures;
        return;
    }
    int r, g, b, h, s, v, n = 0, bad = 0;
    while (std::fscanf(f, "%d %d %d %d %d %d", &r, &g, &b, &h, &s, &v) == 6) {
        ++n;
        const Hsv px = rgb_to_hsv_8u(r, g, b);
        if (px.h != h || px.s != s || px.v != v) {
            ++bad;
            if (bad <= 5)
                std::printf("FAIL: vector rgb=%d,%d,%d got=%d,%d,%d "
                            "want=%d,%d,%d\n",
                            r, g, b, px.h, px.s, px.v, h, s, v);
        }
    }
    std::fclose(f);
    std::printf("vectors: %d checked, %d mismatches\n", n, bad);
    ++g_checks;
    if (n == 0 || bad > 0) ++g_failures;
}

// ---------------- gate params (hue wraparound) ----------------

void test_gate_params() {
    const Tolerance tol;  // 40, 100, 100

    GateParams p;
    // h=10: lo=-30 < 0 -> [(0,50),(150,179)]
    check(build_gate_params(10, 50, 200, tol, p), "build h=10");
    ++g_checks;
    if (p.h_ranges.size() != 2 || p.h_ranges[0].lo != 0 ||
        p.h_ranges[0].hi != 50 || p.h_ranges[1].lo != 150 ||
        p.h_ranges[1].hi != 179) {
        ++g_failures;
        std::printf("FAIL: h=10 ranges wrong: %zu ranges\n", p.h_ranges.size());
    }
    check_eq(p.s_lo, 0, "h=10 s_lo");
    check_eq(p.s_hi, 150, "h=10 s_hi");
    check_eq(p.v_lo, 100, "h=10 v_lo");
    check_eq(p.v_hi, 255, "h=10 v_hi");
    check(p.enabled, "h=10 enabled");

    // h=179: hi=219 > 179 -> [(139,179),(0,39)]
    check(build_gate_params(179, 128, 64, tol, p), "build h=179");
    ++g_checks;
    if (p.h_ranges.size() != 2 || p.h_ranges[0].lo != 139 ||
        p.h_ranges[0].hi != 179 || p.h_ranges[1].lo != 0 ||
        p.h_ranges[1].hi != 39) {
        ++g_failures;
        std::printf("FAIL: h=179 ranges wrong\n");
    }
    check_eq(p.s_lo, 28, "h=179 s_lo");
    check_eq(p.s_hi, 228, "h=179 s_hi");
    check_eq(p.v_lo, 0, "h=179 v_lo");

    // h=90 (no wrap) -> single range [(50,130)]
    check(build_gate_params(90, 0, 255, tol, p), "build h=90");
    ++g_checks;
    if (p.h_ranges.size() != 1 || p.h_ranges[0].lo != 50 ||
        p.h_ranges[0].hi != 130) {
        ++g_failures;
        std::printf("FAIL: h=90 ranges wrong\n");
    }

    // h=0 -> [(-40,40)] -> [(0,40),(140,179)]
    check(build_gate_params(0, 10, 10, tol, p), "build h=0");
    ++g_checks;
    if (p.h_ranges.size() != 2 || p.h_ranges[0].lo != 0 ||
        p.h_ranges[0].hi != 40 || p.h_ranges[1].lo != 140 ||
        p.h_ranges[1].hi != 179) {
        ++g_failures;
        std::printf("FAIL: h=0 ranges wrong\n");
    }

    // Saturation floor: h>=40 && h<=139 -> no wrap, one range.
    check(build_gate_params(45, 200, 20, tol, p), "build h=45");
    ++g_checks;
    if (p.h_ranges.size() != 1 || p.h_ranges[0].lo != 5 ||
        p.h_ranges[0].hi != 85) {
        ++g_failures;
        std::printf("FAIL: h=45 ranges wrong\n");
    }

    // Out-of-domain input must be rejected, not produce garbage.
    check(!build_gate_params(180, 0, 0, tol, p), "reject h=180");
    check(!build_gate_params(-1, 0, 0, tol, p), "reject h=-1");
    check(!build_gate_params(10, 256, 0, tol, p), "reject s=256");
    check(!build_gate_params(10, 0, -1, tol, p), "reject v=-1");

    // Zero tolerance -> single-point range.
    const Tolerance tol0{0, 0, 0};
    check(build_gate_params(77, 33, 44, tol0, p), "build tol0");
    ++g_checks;
    if (p.h_ranges.size() != 1 || p.h_ranges[0].lo != 77 ||
        p.h_ranges[0].hi != 77) {
        ++g_failures;
        std::printf("FAIL: tol0 range wrong\n");
    }
}

// ---------------- angle math ----------------

void test_angle() {
    check_eq(gate_angle(10, 5, 5, 5), 0, "angle right");
    check_eq(gate_angle(5, 3, 5, 5), 90, "angle up");
    check_eq(gate_angle(2, 5, 5, 5), 180, "angle left");
    check_eq(gate_angle(5, 7, 5, 5), 270, "angle down");
    check_eq(gate_angle(8, 2, 5, 5), 45, "angle up-right 45");
    // atan2(5-8, 2-5) = atan2(-3,-3) = -135 deg -> 225 (below-left)
    check_eq(gate_angle(2, 8, 5, 5), 225, "angle below-left 225");
    // just below the +x axis: atan2(-1, 5) = -11.31 deg -> 348
    check_eq(gate_angle(10, 6, 5, 5), 348, "angle wraps to 348");
    // upper-left: atan2(1, -3) = 161.57 -> 161
    check_eq(gate_angle(2, 4, 5, 5), 161, "angle 161");

    check_eq(angle_diff_wrapped(1, 359), 2, "diff 1 vs 359");
    check_eq(angle_diff_wrapped(359, 1), 2, "diff 359 vs 1");
    check_eq(angle_diff_wrapped(0, 3), 3, "diff 0 vs 3");
    check_eq(angle_diff_wrapped(0, 4), 4, "diff 0 vs 4");
    check_eq(angle_diff_wrapped(180, 0), 180, "diff 180 vs 0");
    check_eq(angle_diff_wrapped(90, 270), 180, "diff 90 vs 270");
    check_eq(angle_diff_wrapped(100, 100), 0, "diff equal");
    check_eq(angle_diff_wrapped(355, 4), 9, "diff 355 vs 4");
}

// ---------------- serial policy ----------------

void test_serial_policy() {
    SerialGateState st;

    // First detection -> immediate send (nothing sent yet).
    SerialDecision d = decide_serial(st, true, 100, 456.7, 10.0);
    check(d.action == SerialAction::SendAngle, "first detection sends");
    check_eq(d.angle, 100, "first angle");
    check_eq(d.area, 456, "area truncated like Python int()");

    // Write failure: no commit -> same decision next frame.
    SerialDecision d2 = decide_serial(st, true, 100, 456.7, 10.03);
    check(d2.action == SerialAction::SendAngle, "retry after failed write");
    commit_sent_angle(st, 100, 10.0);

    // Too soon for the interval.
    d = decide_serial(st, true, 110, 456.7, 10.05);
    check(d.action == SerialAction::None, "min send interval");

    // Interval passed but angle change below threshold.
    d = decide_serial(st, true, 101, 456.7, 10.2);
    check(d.action == SerialAction::None, "angle below threshold");

    // Change by threshold after interval -> send.
    d = decide_serial(st, true, 103, 456.7, 10.3);
    check(d.action == SerialAction::SendAngle, "angle >= threshold sends");
    commit_sent_angle(st, 103, 10.3);

    // Wrap-aware: 359 -> 1 is 2 deg (must NOT send).
    commit_sent_angle(st, 359, 20.0);
    d = decide_serial(st, true, 1, 456.7, 20.2);
    check(d.action == SerialAction::None,
          "359 -> 1 is 2 deg, no send (wrap fix vs Python)");

    // 359 -> 2 is 3 deg (must send).
    d = decide_serial(st, true, 2, 456.7, 20.3);
    check(d.action == SerialAction::SendAngle, "359 -> 2 is 3 deg, send");
    commit_sent_angle(st, 2, 20.3);

    // Lost after a sent detection -> exactly one lost line.
    d = decide_serial(st, false, -1, 0.0, 21.0);
    check(d.action == SerialAction::SendLost, "lost after found sends -1,0");
    commit_sent_lost(st);
    d = decide_serial(st, false, -1, 0.0, 21.1);
    check(d.action == SerialAction::None, "lost is sent once");

    // Detection resumes -> new angle send.
    d = decide_serial(st, true, 200, 456.7, 22.0);
    check(d.action == SerialAction::SendAngle, "found again after lost");

    // Heartbeat schedule.
    check(!heartbeat_due(0.0, 0.4), "heartbeat not due at 0.4");
    check(heartbeat_due(0.0, 0.5), "heartbeat due at 0.5");
    check(!heartbeat_due(1.0, 1.49), "heartbeat not due at 1.49");
    check(heartbeat_due(1.0, 1.5), "heartbeat due at 1.5");

    // Contract formats (byte-exact, section 4).
    check_str(format_gate_line(1, 123, 456), "G1,123,456\n", "gate line");
    check_str(format_gate_line(3, 0, 9), "G3,0,9\n", "gate line 3");
    check_str(format_lost_line(2), "G2,-1,0\n", "lost line");
    check_str(std::string(kHeartbeatLine), "HB\n", "heartbeat line");
}

// ---------------- colors JSON ----------------

void test_color_message() {
    ColorsMessage m = parse_colors_message(
        "{\"gates\":[{\"id\":1,\"rgb\":[255,220,0]},{\"id\":2,\"rgb\":[0,0,"
        "255]}]}");
    check(m.valid, "gates msg valid");
    check_eq(static_cast<int>(m.gates.size()), 2, "two gates");
    ++g_checks;
    if (m.gates.size() == 2) {
        check(m.gates[0].first == 1 && m.gates[0].second[0] == 255 &&
                  m.gates[0].second[1] == 220 && m.gates[0].second[2] == 0,
              "gate 1 rgb");
        check(m.gates[1].first == 2 && m.gates[1].second[0] == 0,
              "gate 2 rgb");
    }

    m = parse_colors_message("{\"rgb\":[10,20,30]}");
    check(m.valid && m.has_legacy_rgb, "legacy rgb");
    check_eq(m.legacy_rgb[2], 30, "legacy b");

    // "gates" key present -> legacy "rgb" is NOT applied (Python elif).
    m = parse_colors_message(
        "{\"gates\":[],\"rgb\":[10,20,30]}");
    check(m.valid && !m.has_legacy_rgb && m.gates.empty(), "elif semantics");

    m = parse_colors_message("{\"stream\":false}");
    check(m.valid && m.has_stream && !m.stream_value, "stream false");

    m = parse_colors_message("{\"stream\":1}");
    check(m.valid && m.has_stream && m.stream_value, "stream truthy int");

    m = parse_colors_message("{\"stream\":\"\"}");
    check(m.valid && m.has_stream && !m.stream_value, "stream empty string");

    m = parse_colors_message("{\"gates\":[{\"rgb\":[5,6,7]}]}");
    check(m.valid && m.gates.size() == 1 && m.gates[0].first == 1,
          "missing id defaults to 1");

    m = parse_colors_message("{\"gates\":[{\"id\":4,\"rgb\":[1,2,3]}]}");
    check(m.valid && m.gates.size() == 1 && m.gates[0].first == 4,
          "id=4 parsed (caller filters vs --gates)");

    m = parse_colors_message("{\"gates\":[{\"id\":2,\"rgb\":[1,2]}]}");
    check(m.valid && m.gates.empty(), "short rgb skipped");

    m = parse_colors_message("not json");
    check(!m.valid, "garbage rejected");

    m = parse_colors_message("[1,2,3]");
    check(!m.valid, "array rejected");

    m = parse_colors_message("");
    check(!m.valid, "empty rejected");

    // rgb clamped to 0..255 so a bad color can't produce a degenerate range.
    m = parse_colors_message("{\"rgb\":[300,-5,0]}");
    check(m.valid && m.has_legacy_rgb && m.legacy_rgb[0] == 255 &&
              m.legacy_rgb[1] == 0,
          "rgb clamped");

    // ---------------- Idea 9: optional "role" per gate (additive) --------
    m = parse_colors_message(
        "{\"gates\":[{\"id\":1,\"rgb\":[1,2,3],\"role\":\"ours\"}]}");
    check(m.valid && m.gates.size() == 1 && m.gate_roles.size() == 1 &&
              m.gate_roles[0] == "ours",
          "role parsed when set");
    m = parse_colors_message(
        "{\"gates\":[{\"id\":1,\"rgb\":[1,2,3]}]}");
    check(m.gate_roles.size() == 1 && m.gate_roles[0].empty(),
          "absent role -> empty (1:1 with gates)");
    m = parse_colors_message(
        "{\"gates\":[{\"id\":1,\"rgb\":[1,2,3],\"role\":\"junk\"},"
        "{\"id\":2,\"rgb\":[4,5,6],\"role\":\"opponent\"}]}");
    check(m.gates.size() == 2 && m.gate_roles.size() == 2 &&
              m.gate_roles[0].empty() && m.gate_roles[1] == "opponent",
          "invalid role ignored, valid kept, 1:1 indexing");
    m = parse_colors_message(
        "{\"gates\":[{\"id\":1,\"rgb\":[1,2,3],\"role\":\"unset\"}]}");
    check(m.gate_roles.size() == 1 && m.gate_roles[0].empty(),
          "'unset' on the wire treated as not set");
    check(gate_role_from_string("ours") == GateRole::Ours &&
              gate_role_from_string("opponent") == GateRole::Opponent &&
              gate_role_from_string("unset") == GateRole::Unset &&
              gate_role_from_string("") == GateRole::Unset &&
              gate_role_from_string("garbage") == GateRole::Unset,
          "gate_role_from_string mapping");
}

// ---------------- Idea 5: blob clustering + hysteresis ----------------

GateComponent gcomp(double cx, double cy, double area, double x, double y,
                    double w, double h) {
    GateComponent c;
    c.cx = cx;
    c.cy = cy;
    c.area = area;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    return c;
}

void test_gate_cluster() {
    // resolve: width-scaled defaults at 640, odd kernel fix.
    const ClusterParams rp = resolve_cluster_params(640, 360, 0.0, 0.0, 0.0,
                                                    4, 0, 0);
    check_eq(static_cast<int>(rp.merge_gap_px), 16, "resolve merge_gap 2.5%");
    check_eq(static_cast<int>(rp.max_stray_px), 64, "resolve max_stray 10%");
    check_eq(rp.open_kernel, 5, "even kernel -> odd");
    check_eq(rp.lost_frames, 1, "lost_frames min 1");
    check_eq(rp.confirm_frames, 1, "confirm_frames min 1");
    check(std::fabs(rp.min_area_px) < 1e-12, "min_area 0 stays 0 (filter off)");

    const ClusterParams p = resolve_cluster_params(640, 360, 16.0, 100.0,
                                                   64.0, 3, 5, 3);

    // rect_gap: overlapping boxes -> 0; diagonal gap is exact.
    check_eq(static_cast<int>(rect_gap(0, 0, 10, 10, 5, 5, 10, 10)), 0,
             "rect_gap overlapping");
    check(std::fabs(rect_gap(0, 0, 10, 10, 15, 0, 10, 10) - 5.0) < 1e-12,
          "rect_gap horizontal 5");
    check(std::fabs(rect_gap(0, 0, 10, 10, 13, 14, 10, 10) -
                    std::sqrt(3.0 * 3.0 + 4.0 * 4.0)) < 1e-12,
          "rect_gap diagonal 3-4-5");

    // angle_from_center: float centroid; 0 deg = right, CCW; truncation.
    check_eq(angle_from_center(289.7, 119.5, 320, 180), 116,
             "angle float centroid");
    check_eq(angle_from_center(330.0, 180.0, 320, 180), 0, "angle right");
    check_eq(angle_from_center(320.0, 175.0, 320, 180), 90, "angle up");
    // atan2(cy0-cy, cx-cx0) = atan2(-3, -5) = -149.04 deg -> 360-149.04 = 210.96
    check_eq(angle_from_center(315.0, 183.0, 320, 180), 210,
             "angle atan2(-3,-5)*180/pi % 360");

    // merge: two pieces with a 16 px gap (<= merge_gap) -> ONE gate object.
    std::vector<GateComponent> comps = {
        gcomp(240, 120, 3200, 200, 100, 80, 40),
        gcomp(338, 120, 3360, 296, 100, 84, 40),
        gcomp(65, 320, 2000, 40, 300, 50, 40),  // far stray of the same color
    };
    GateObject det = build_gate_object(comps, p, false, 0, 0);
    check(det.valid, "build_gate_object valid");
    check_eq(det.n_components, 2, "stray rejected, pieces merged");
    check(std::fabs(det.area - 6560.0) < 1e-9, "merged area");
    check(std::fabs(det.cx - (3200.0 * 240 + 3360.0 * 338) / 6560.0) < 1e-9,
          "exact area-weighted centroid");
    check(std::fabs(det.radius - 0.5 * std::sqrt(180.0 * 180.0 + 40.0 * 40.0)) <
              1e-9,
          "radius = union-box circumcircle");
    check_eq(static_cast<int>(det.w), 180, "union box width");

    // anchor preference: prev near the stray -> stray becomes the gate.
    det = build_gate_object(comps, p, true, 65.0, 320.0);
    check_eq(det.n_components, 1, "prev picks the nearest cluster");
    check(std::fabs(det.area - 2000.0) < 1e-9, "prev anchor area");

    // gap one px over the limit stays a separate (stray) cluster.
    std::vector<GateComponent> apart = {
        gcomp(240, 120, 3200, 200, 100, 80, 40),
        gcomp(382, 120, 100, 340, 100, 84, 40)};
    det = build_gate_object(apart, p, false, 0, 0);
    check_eq(det.n_components, 1, "gap > merge_gap: far piece discarded");

    // empty input.
    check(!build_gate_object({}, p, false, 0, 0).valid, "empty -> invalid");

    // ---- hysteresis ----
    GateObject pub;
    GateStability st(5, 3, 64.0);
    GateObject g1 = build_gate_object(comps, p, false, 0, 0);
    g1.cx = 100.0;
    g1.cy = 180.0;

    // Fresh tracker: first detection accepted immediately (legacy behavior).
    check(st.update(&g1, pub) && !st.last_held(), "fresh: immediate accept");
    check(std::fabs(pub.cx - g1.cx) < 1e-12, "fresh publishes the object");

    // Empty frames: 4 grace frames (held), loss fires exactly on the 5th.
    // update() returns TRUE during grace (the held object is published).
    bool saw_held = false;
    for (int k = 1; k <= 4; ++k) {
        check(st.update(nullptr, pub) && st.last_held(),
              "grace: held published, not lost yet");
        if (st.last_held()) saw_held = true;
    }
    check(saw_held, "grace publishes held");
    check(!st.update(nullptr, pub) && st.lost_event(),
          "5th empty frame: loss event");
    check(!st.update(nullptr, pub), "stays lost");
    check(!st.lost_event(), "loss event is one-shot");

    // Re-acception after M consecutive frames.
    check(!st.update(&g1, pub), "reappear 1/M: pending");
    check(!st.update(&g1, pub), "reappear 2/M: pending");
    check(st.update(&g1, pub) && !st.last_held(), "reappear 3/M: accepted");

    // Far new object takes over after M frames WITHOUT a loss blip.
    GateObject g2 = g1;
    g2.cx = 500.0;
    g2.cy = 100.0;
    check(st.update(&g2, pub) && st.last_held(),
          "far candidate 1: held published");
    check(st.update(&g2, pub) && st.last_held(), "far candidate 2: held");
    check(st.update(&g2, pub) && !st.last_held() &&
              std::fabs(pub.cx - 500.0) < 1e-12,
          "far candidate 3: stable replacement accepted, no lost_event");
    check(!st.lost_event(), "no loss blip during the switch");

    // Flickering far object never confirms -> loss after N.
    // empty_count: far(1), empty(2), far(3), empty(4), empty(5) -> loss.
    GateStability st2(5, 3, 64.0);
    check(st2.update(&g1, pub), "st2: accept");
    GateObject far = g2;
    check(st2.update(&far, pub) && st2.last_held(), "st2: far -> held");
    check(st2.update(nullptr, pub) && st2.last_held(), "st2: empty -> held");
    check(!st2.lost_event(), "st2: no loss yet");
    check(st2.update(&far, pub) && st2.last_held(),
          "st2: far again -> pending restart");
    check(st2.update(nullptr, pub) && st2.last_held(), "st2: empty -> held");
    check(!st2.update(nullptr, pub) && st2.lost_event(),
          "st2: loss after N empties");
    check(!st2.update(nullptr, pub) && !st2.lost_event(), "st2: stays lost");
}

// Parity mode: read the vectors file produced by out/parity_check5.py,
// run build_gate_object + GateStability and print one R line per frame for
// the Python side to compare (same file format as --vectors for HSV).
//
// Format:
//   P <merge_gap> <min_area> <max_stray> <lost> <confirm>
//   F <n_components>
//   C <cx> <cy> <area> <x> <y> <w> <h>     (n lines per frame)
// Output:
//   R <found> <held> <cx> <cy> <area> <radius> <w>   (or R 0 0 - - - - -)
void test_cluster_parity_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("FAIL: cannot open parity file %s\n", path);
        ++g_failures;
        return;
    }
    char tag[8] = {0};
    ClusterParams p;
    if (std::fscanf(f, " %7s %lf %lf %lf %d %d", tag, &p.merge_gap_px,
                    &p.min_area_px, &p.max_stray_px, &p.lost_frames,
                    &p.confirm_frames) != 6 ||
        tag[0] != 'P') {
        std::printf("FAIL: bad params line in %s\n", path);
        ++g_failures;
        std::fclose(f);
        return;
    }
    GateStability st(p.lost_frames, p.confirm_frames, p.max_stray_px);
    int frames = 0;
    for (;;) {
        char t[8] = {0};
        if (std::fscanf(f, " %7s", t) != 1) break;
        if (t[0] != 'F') break;  // only F frames are expected
        int n = 0;
        if (std::fscanf(f, "%d", &n) != 1) {
            std::printf("FAIL: bad F line\n");
            ++g_failures;
            break;
        }
        std::vector<GateComponent> comps;
        comps.reserve(static_cast<size_t>(n));
        bool bad = false;
        for (int i = 0; i < n; ++i) {
            char ctag[8] = {0};
            GateComponent c;
            if (std::fscanf(f, " %7s %lf %lf %lf %lf %lf %lf %lf", ctag,
                            &c.cx, &c.cy, &c.area, &c.x, &c.y, &c.w,
                            &c.h) != 8 || ctag[0] != 'C') {
                bad = true;
                break;
            }
            comps.push_back(c);
        }
        if (bad) {
            std::printf("FAIL: bad C line\n");
            ++g_failures;
            break;
        }
        ++frames;
        const GateObject det = build_gate_object(
            comps, p, st.has_prev(), st.prev_x(), st.prev_y());
        GateObject pub;
        const bool published = st.update(det.valid ? &det : nullptr, pub);
        if (published) {
            std::printf("R 1 %d %.3f %.3f %.3f %.3f %.3f\n",
                        st.last_held() ? 1 : 0, pub.cx, pub.cy, pub.area,
                        pub.radius, pub.w);
        } else {
            std::printf("R 0 0 - - - - -\n");
        }
    }
    std::fclose(f);
    std::printf("PARITY-FRAMES %d\n", frames);
}

// ---------------- gate store ----------------

void test_gate_store(const char* tmpdir) {
    const std::string path =
        std::string(tmpdir) + "/pi_core_test_saved_gates.json";

    // Round trip.
    {
        GateStore store;
        store.set_gate_from_network(1, {255, 220, 0});
        store.set_gate_from_network(2, {0, 90, 255});
        check(store.dirty(), "store dirty after set");
        check(store.save(path), "store save");
        store.clear_dirty();
        check(!store.dirty(), "store clean after clear");

        GateStore loaded;
        check(loaded.load(path), "store load");
        const GateRecord r1 = loaded.record(1);
        check(r1.present && r1.enabled && r1.has_rgb, "record 1 present");
        ++g_checks;
        if (r1.has_rgb) {
            check(r1.rgb[0] == 255 && r1.rgb[1] == 220 && r1.rgb[2] == 0,
                  "record 1 rgb round trip");
        }
        check(!r1.ts.empty(), "record 1 ts stamped");
        // save() always writes all three slots, so slot 3 is present but empty.
        const GateRecord r3 = loaded.record(3);
        check(r3.present && !r3.enabled && !r3.has_rgb,
              "record 3 present-but-empty");
    }

    // Missing file.
    {
        GateStore loaded;
        check(!loaded.load(std::string(tmpdir) +
                           "/pi_core_definitely_missing.json"),
              "missing file -> false");
    }

    // Corrupt file.
    {
        check(atomic_write(path, "{not valid json"), "corrupt write");
        GateStore loaded;
        check(!loaded.load(path), "corrupt file -> false");
    }

    // Unknown/extra fields (role, Idea 9) survive a Pi-side color update.
    {
        check(atomic_write(
                  path,
                  "{\"version\":1,\"gates\":{\"1\":{\"rgb\":[1,2,3],\"lab\":"
                  "null,\"enabled\":true,\"ts\":\"2026-01-01T00:00:00\","
                  "\"role\":\"ours\"}}}"),
              "role preset write");
        GateStore loaded;
        check(loaded.load(path), "role load");
        const GateRecord r1 = loaded.record(1);
        check_str(r1.role, "ours", "role loaded");
        loaded.set_gate_from_network(1, {9, 8, 7});
        check(loaded.save(path), "role save");

        std::string text;
        check(read_file(path, text), "role read back");
        check(text.find("\"role\"") != std::string::npos &&
                  text.find("ours") != std::string::npos,
              "role preserved in saved JSON");

        GateStore reloaded;
        check(reloaded.load(path), "role reload");
        check_str(reloaded.record(1).role, "ours", "role round trip");
        const GateRecord rr = reloaded.record(1);
        check(rr.has_rgb && rr.rgb[0] == 9 && rr.rgb[1] == 8 && rr.rgb[2] == 7,
              "rgb updated");
        check(rr.enabled, "rgb update enables the gate");
    }

    // Idea 9: set_gate_from_network applies the optional role argument.
    {
        GateStore store;
        store.set_gate_from_network(1, {1, 2, 3}, "ours");
        check_str(store.record(1).role, "ours", "network role set");
        store.set_gate_from_network(2, {4, 5, 6}, "opponent");
        check_str(store.record(2).role, "opponent", "network role opponent");
        store.set_gate_from_network(1, {7, 8, 9});  // no role -> keep
        check_str(store.record(1).role, "ours", "absent role keeps previous");
        store.set_gate_from_network(2, {4, 5, 6}, "weird");  // unknown -> keep
        check_str(store.record(2).role, "opponent",
                  "unknown role keeps previous");
        store.set_gate_from_network(1, {7, 8, 9}, "unset");  // explicit clear
        check_str(store.record(1).role, "", "explicit unset clears role");
        check(store.save(path), "network role save");
        GateStore loaded;
        check(loaded.load(path), "network role load");
        check_str(loaded.record(2).role, "opponent",
                  "network role round trip");
    }

    // Atomicity: no leftover tmp file after a save.
    {
        GateStore store;
        store.set_gate_from_network(3, {250, 180, 30});
        check(store.save(path), "atomic save");
        std::string text;
        check(read_file(path, text), "atomic read");
        check(text.find("250") != std::string::npos, "atomic content");
        // The .tmp file must not survive.
        FILE* f = std::fopen((path + ".tmp").c_str(), "rb");
        check(f == nullptr, "no leftover .tmp");
        if (f) std::fclose(f);
    }

    std::remove(path.c_str());
}

void test_atomic_file(const char* tmpdir) {
    const std::string path = std::string(tmpdir) + "/pi_core_test_atomic.txt";
    check(atomic_write(path, "hello"), "atomic_write first");
    std::string out;
    check(read_file(path, out) && out == "hello", "atomic read back");
    check(atomic_write(path, "world!"), "atomic_write overwrite");
    check(read_file(path, out) && out == "world!", "atomic overwrite");
    check(atomic_write("/nonexistent_dir_zz/file.txt", "x") == false,
          "atomic_write to bad dir fails");
    std::remove(path.c_str());
}

// ---------------- Idea 8: self-mask (robot body exclusion) ----------------

void test_self_mask() {
    // resolve_self_mask defaults (mirror self_mask.py resolve_params):
    // 640x360 -> center (320, 180), r = 0.22 * 360 = 79.2 px, no annulus.
    {
        const SelfMaskParams p =
            resolve_self_mask(640, 360, true, 0, 0, 0, 0);
        check(p.enabled, "resolve: enabled");
        check(std::fabs(p.cx - 320.0) < 1e-9, "resolve: cx = w/2",
              std::to_string(p.cx));
        check(std::fabs(p.cy - 180.0) < 1e-9, "resolve: cy = h/2",
              std::to_string(p.cy));
        check(std::fabs(p.r - 0.22 * 360.0) < 1e-9,
              "resolve: r = 0.22*min(w,h)", std::to_string(p.r));
        check(p.r2 == 0.0, "resolve: no annulus by default");
    }
    // Odd size: center lands on x.5 fractions; min(w, h) picks the smaller.
    {
        const SelfMaskParams p =
            resolve_self_mask(721, 361, true, 0, 0, 0, 0);
        check(std::fabs(p.cx - 360.5) < 1e-9, "resolve: odd-size center x",
              std::to_string(p.cx));
        check(std::fabs(p.cy - 180.5) < 1e-9, "resolve: odd-size center y",
              std::to_string(p.cy));
        check(std::fabs(p.r - 0.22 * 361.0) < 1e-9,
              "resolve: r from min dimension", std::to_string(p.r));
    }
    // Explicit px values pass through; portrait laptop frame 720x1280.
    {
        const SelfMaskParams p =
            resolve_self_mask(720, 1280, true, 100.5, 200.25, 150.0, 0);
        check(std::fabs(p.cx - 100.5) < 1e-9, "resolve: explicit cx");
        check(std::fabs(p.cy - 200.25) < 1e-9, "resolve: explicit cy");
        check(std::fabs(p.r - 150.0) < 1e-9, "resolve: explicit r");
    }
    // Annulus rules: r2 <= r collapses to 0; r2 > r is kept.
    {
        const SelfMaskParams p1 =
            resolve_self_mask(640, 360, true, 0, 0, 80, 80);
        check(p1.r2 == 0.0, "resolve: r2 == r collapses to disk");
        const SelfMaskParams p2 =
            resolve_self_mask(640, 360, true, 0, 0, 80, 50);
        check(p2.r2 == 0.0, "resolve: r2 < r collapses to disk");
        const SelfMaskParams p3 =
            resolve_self_mask(640, 360, true, 0, 0, 80, 200);
        check(std::fabs(p3.r2 - 200.0) < 1e-9, "resolve: real annulus kept");
    }
    // Disabled (--no-self-mask): params stay off, keep mask all-searchable.
    {
        const SelfMaskParams p = resolve_self_mask(640, 360, false, 0, 0, 0, 0);
        check(!p.enabled, "resolve: --no-self-mask disables");
        std::vector<std::uint8_t> buf(640 * 360, 0);
        build_keep_mask(p, 640, 360, buf.data());
        bool all_keep = true;
        for (std::uint8_t v : buf)
            if (v != 255) all_keep = false;
        check(all_keep, "disabled keep mask = all 255");
    }

    // Pure point test: disk r=80 centered at (100, 100).
    {
        const SelfMaskParams p = resolve_self_mask(200, 200, true, 100, 100, 80, 0);
        check(self_mask_excludes(p, 100.0, 100.0), "point: center excluded");
        check(self_mask_excludes(p, 100.0 + 79.0, 100.0), "point: r-1 excluded");
        check(self_mask_excludes(p, 100.0, 100.0 - 80.0), "point: on the circle excluded");
        check(!self_mask_excludes(p, 100.0 + 81.0, 100.0), "point: r+1 kept");
        check(!self_mask_excludes(p, 199.0, 199.0), "point: far corner kept");
        check(!self_mask_excludes(p, 0.0, 0.0), "point: corner kept");
        // Annulus 80..140: the SEARCHABLE ring is r < d < r2 (PROMPT §6:
        // "blob between inner and outer radius detected, inside inner
        // excluded, outside outer excluded" — the reference mask r 110..355
        // is exactly this search ring around the body).
        const SelfMaskParams a =
            resolve_self_mask(300, 300, true, 150, 150, 80, 140);
        check(self_mask_excludes(a, 150.0, 150.0), "annulus: center excluded");
        check(!self_mask_excludes(a, 150.0 + 100.0, 150.0),
              "annulus: between r and r2 searchable");
        check(!self_mask_excludes(a, 150.0 + 139.0, 150.0),
              "annulus: just inside r2 searchable");
        check(self_mask_excludes(a, 150.0 + 140.0, 150.0),
              "annulus: on the outer circle excluded");
        check(self_mask_excludes(a, 150.0 + 141.0, 150.0),
              "annulus: beyond r2 excluded");
    }

    // build_keep_mask buffer semantics on a small frame (64x48, r=10):
    // center zero, far corner 255, excluded-count == pure point-test count
    // (the same equation), and an AND with a synthetic blob mask zeroes the
    // part on the body while keeping the part outside (what gate_tracker
    // does with cv::bitwise_and before morphology/CC).
    {
        const int w = 64, h = 48;
        const SelfMaskParams p = resolve_self_mask(w, h, true, 32, 24, 10, 0);
        std::vector<std::uint8_t> keep(static_cast<size_t>(w) * h, 0);
        build_keep_mask(p, w, h, keep.data());
        check(keep[24 * w + 32] == 0, "keep: body center zeroed");
        check(keep[0] == 255, "keep: corner searchable");
        check(keep[24 * w + 63] == 255, "keep: right edge searchable");

        long buf_excluded = 0;
        long point_excluded = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (keep[static_cast<size_t>(y) * w + x] == 0) ++buf_excluded;
                if (self_mask_excludes(p, x, y)) ++point_excluded;
            }
        }
        check(buf_excluded == point_excluded,
              "keep mask count == pure point-test count",
              std::to_string(buf_excluded) + " vs " +
                  std::to_string(point_excluded));
        check(buf_excluded > 0, "keep mask excludes something");

        // Integration semantics: a blob covering the body zone AND a real
        // gate region — after the AND only the outside part survives.
        std::vector<std::uint8_t> blob(static_cast<size_t>(w) * h, 0);
        for (int y = 20; y < 28; ++y)          // 8x14 rect across the center
            for (int x = 26; x < 40; ++x)
                blob[static_cast<size_t>(y) * w + x] = 255;
        long before = 0;
        for (std::uint8_t v : blob)
            if (v) ++before;
        for (int i = 0; i < w * h; ++i)
            blob[i] = static_cast<std::uint8_t>(blob[i] & keep[i]);
        long after = 0;
        for (std::uint8_t v : blob)
            if (v) ++after;
        check(after < before, "AND with keep mask shrinks the body blob",
              std::to_string(before) + " -> " + std::to_string(after));
        bool kept_ok = true;
        for (int y = 20; y < 28; ++y) {
            for (int x = 26; x < 40; ++x) {
                const bool should =
                    !self_mask_excludes(p, x, y) ? blob[static_cast<size_t>(y) * w + x] == 255
                                                 : blob[static_cast<size_t>(y) * w + x] == 0;
                if (!should) kept_ok = false;
            }
        }
        check(kept_ok, "AND result matches the point test per pixel");
    }

    // Deterministic rebuild: same params -> byte-identical keep mask.
    {
        const int w = 100, h = 80;
        const SelfMaskParams p = resolve_self_mask(w, h, true, 50, 40, 30, 60);
        std::vector<std::uint8_t> k1(static_cast<size_t>(w) * h),
            k2(static_cast<size_t>(w) * h);
        build_keep_mask(p, w, h, k1.data());
        build_keep_mask(p, w, h, k2.data());
        check(k1 == k2, "keep mask rebuild deterministic (annulus)");
    }
}

// Idea 8 parity driver (out/selfmask_check.py): the file contains lines
//   case W H CX CY R R2 ENABLED      (start a case; values are the resolved
//                                     px circle — applied verbatim)
//   probe PX PY                      (point test for the current case)
// For every case the C++ prints "case W H excluded=<n>" (zeros in the keep
// mask) and "probe PX PY <0|1>" — the Python side computes the same values
// with the identical IEEE-754 equation and must match bit-for-bit.
void test_self_mask_parity_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("FAIL: cannot open selfmask parity file %s\n", path);
        ++g_failures;
        return;
    }
    char kind[16];
    bool have_case = false;
    SelfMaskParams p;
    int case_w = 0, case_h = 0;
    while (std::fscanf(f, "%15s", kind) == 1) {
        if (std::strcmp(kind, "case") == 0) {
            double w, h, cx, cy, r, r2;
            int enabled;
            if (std::fscanf(f, "%lf %lf %lf %lf %lf %lf %d", &w, &h, &cx, &cy,
                            &r, &r2, &enabled) != 7) {
                std::printf("FAIL: malformed case line\n");
                ++g_failures;
                break;
            }
            have_case = true;
            case_w = static_cast<int>(w);
            case_h = static_cast<int>(h);
            p.enabled = enabled != 0;
            p.cx = cx;
            p.cy = cy;
            p.r = r;
            p.r2 = (r2 > r) ? r2 : 0.0;
            std::vector<std::uint8_t> keep(
                static_cast<size_t>(case_w) * case_h, 0);
            build_keep_mask(p, case_w, case_h, keep.data());
            long excluded = 0;
            for (std::uint8_t v : keep)
                if (v == 0) ++excluded;
            std::printf("case %dx%d excluded=%ld\n", case_w, case_h,
                        excluded);
        } else if (std::strcmp(kind, "probe") == 0) {
            double px, py;
            if (std::fscanf(f, "%lf %lf", &px, &py) != 2) {
                std::printf("FAIL: malformed probe line\n");
                ++g_failures;
                break;
            }
            const int v = (have_case && self_mask_excludes(p, px, py)) ? 1 : 0;
            std::printf("probe %.2f %.2f %d\n", px, py, v);
        } else {
            std::printf("FAIL: unknown parity token %s\n", kind);
            ++g_failures;
            break;
        }
    }
    std::fclose(f);
}

// ---------------- Idea 6: distance model / fit / guards / hold ----------------

// Reference points for a plausible calibration: gate width 45 cm, f = 640/pi,
// r ~= half of w_px of a goal 45x20 cm; the model truth below generates them.
static const double kTruthA = -5000.0;
static const double kTruthB = 2.0;
static const double kTruthC = -1.5;

static double truth_d(double r) { return kTruthA / (kTruthB - r) - kTruthC; }

void test_distance(const char* tmpdir) {
    // 1. Round-trip: synthetic a,b,c -> points -> fit recovers them.
    std::vector<double> d, r;
    const double rs[] = {30.0, 45.0, 70.0, 100.0, 140.0};
    for (double rv : rs) {
        r.push_back(rv);
        d.push_back(truth_d(rv));
    }
    DistanceModel m;
    check(fit_distance_model(d, r, 640, 360, m), "fit recovers the model");
    check(m.valid, "fitted model passes validation", m.error);
    {
        const double tol = 1e-6 * std::max(1.0, std::fabs(kTruthA));
        check(std::fabs(m.a - kTruthA) < tol, "fit recovers a",
              std::to_string(m.a));
        check(std::fabs(m.b - kTruthB) < 1e-6, "fit recovers b",
              std::to_string(m.b));
        check(std::fabs(m.c - kTruthC) < 1e-6, "fit recovers c",
              std::to_string(m.c));
        check(m.frame_w == 640 && m.frame_h == 360, "fit stores frame size");
    }

    // 2. Evaluation matches the formula; the guards hold.
    {
        const DistEval ev = eval_distance(m, 70.0, 360);
        check(ev.reliable && std::fabs(ev.dist_cm - truth_d(70.0)) < 1e-9,
              "eval matches the formula");
        const DistEval bad = eval_distance(m, kTruthB, 360);  // division pole
        check(!bad.reliable && std::isnan(bad.dist_cm),
              "division guard: r == b -> unreliable NaN");
        const DistEval junk = eval_distance(m, -5.0, 360);
        check(!junk.reliable && std::isnan(junk.dist_cm), "r <= 0 rejected");
        const DistEval nan_r = eval_distance(m, std::nan(""), 360);
        check(!nan_r.reliable && std::isnan(nan_r.dist_cm), "NaN r rejected");
        // Out of the calibration interval (r=140 is the max point; +25% span
        // extension -> 140+27.5=167.5; r=200 is outside) -> rejected.
        const DistEval far = eval_distance(m, 200.0, 360);
        check(!far.reliable && std::isnan(far.dist_cm),
              "r outside the calibration interval rejected");
        // In-range but beyond the 0..500 working range -> clamped unreliable.
        // truth_d(23.1) = 5000/21.1 + 1.5 ~= 238 ... need r where d > 500:
        // truth_d(12) = 5000/10 + 1.5 = 501.5 (inside interval? min point is
        // 30, span 110, ext 27.5 -> r_eval_lo = 2.5; r=12 >= 2.5 — in range).
        const DistEval near = eval_distance(m, 12.0, 360);
        check(!near.reliable && std::fabs(near.dist_cm - 500.0) < 1e-9,
              "d > 500 clamped to 500 and marked unreliable");
        // Frame scaling: the same scene seen at 2x min-dim scale (r doubles)
        // must give the same distance.
        const DistEval scaled = eval_distance(m, 140.0, 720);
        check(scaled.reliable &&
                  std::fabs(scaled.dist_cm - truth_d(70.0)) < 1e-9,
              "radius scales with the frame min-dim");
    }

    // 3. Garbage fits are rejected, not fitted into a monster.
    {
        DistanceModel g;
        std::vector<double> dr = {100.0, 90.0}, rr = {40.0, 55.0};
        check(!fit_distance_model(dr, rr, 0, 0, g), "2 points are not enough");
        std::vector<double> dr3 = {100.0, 100.0, 100.0}, rr3 = {40.0, 40.0, 40.0};
        check(!fit_distance_model(dr3, rr3, 0, 0, g), "identical points rejected");
        // d increasing with r (points entered backwards) -> a >= 0 -> reject.
        std::vector<double> dr4 = {50.0, 100.0, 150.0, 200.0};
        std::vector<double> rr4 = {30.0, 45.0, 70.0, 100.0};
        check(!fit_distance_model(dr4, rr4, 0, 0, g),
              "increasing d(r) rejected (a >= 0)", g.error);
    }

    // 4. Analytic fallback parity with the legacy unikostyl.py formula
    //    Z = W * (frame_w / pi) / w_px.
    {
        constexpr double kPi = 3.14159265358979323846;  // M_PI is not portable
        const double got = analytic_distance_cm(91.7, 640.0, 45.0);
        const double want = 45.0 * (640.0 / kPi) / 91.7;
        check(std::fabs(got - want) < 1e-9, "analytic fallback formula parity");
        check(analytic_distance_cm(0.0, 640.0, 45.0) < 0.0,
              "analytic fallback rejects junk");
    }

    // 5. Occlusion heuristics + hold (DistEstimator).
    {
        DistEstimator est(2.0 /*hold*/, 0.6 /*drop*/, 0.25 /*fill*/, 0.08,
                          45.0);
        est.set_model(m);
        // Reliable frame: solid object (fill 0.8).
        DistEval ev = est.update(70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, false,
                                 0.0, 360, 640);
        check(ev.reliable && std::fabs(ev.dist_cm - truth_d(70.0)) < 1e-9,
              "estimator: clean frame is reliable");
        // Held frame (mask empty): unreliable, holds the last value.
        ev = est.update(70.0, 90.0, 40.0, 2880.0, 320.0, 180.0, true, 0.04,
                        360, 640);
        check(!ev.reliable && std::fabs(ev.dist_cm - truth_d(70.0)) < 1e-9,
              "estimator: held frame holds the value, unreliable");
        // Sudden radius drop on a STATIONARY centroid -> occlusion suspect:
        // unreliable, holds the last reliable value.
        ev = est.update(35.0, 45.0, 40.0, 1400.0, 321.0, 181.0, false, 0.08,
                        360, 640);
        check(!ev.reliable && std::fabs(ev.dist_cm - truth_d(70.0)) < 1e-9,
              "estimator: sudden drop with stationary centroid -> hold");
        // Same drop but the centroid MOVED away (gates really moved) ->
        // taken at face value (reliable, new reference).
        ev = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, false, 0.12,
                        360, 640);
        check(ev.reliable, "estimator: moved centroid is not occlusion",
              std::to_string(ev.dist_cm));
        // Held within T of the reliable frame at t=0.12.
        ev = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, true, 2.0,
                        360, 640);
        check(!ev.reliable &&
                  std::fabs(ev.dist_cm - truth_d(35.0)) < 1e-9,
              "estimator: held within T of the last reliable frame");
        // Hold window expires -> nothing to report (honest degradation).
        ev = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, true, 2.2,
                        360, 640);
        check(!ev.reliable && std::isnan(ev.dist_cm),
              "estimator: hold expires after T");
        // Recovery: reliable again, hold refreshed.
        ev = est.update(35.0, 45.0, 40.0, 1400.0, 420.0, 200.0, false, 2.4,
                        360, 640);
        check(ev.reliable &&
                  std::fabs(ev.dist_cm - truth_d(35.0)) < 1e-9,
              "estimator: recovery is reliable");
        // Low fill (pieces across the opponent's body): fill 400/1800 < 0.25.
        ev = est.update(35.0, 45.0, 40.0, 400.0, 420.0, 200.0, false, 2.44,
                        360, 640);
        check(!ev.reliable &&
                  std::fabs(ev.dist_cm - truth_d(35.0)) < 1e-9,
              "estimator: low box fill -> unreliable, holds value");
        // Junk radius inside the hold window: hold survives (grace).
        ev = est.update(std::nan(""), 45.0, 40.0, 400.0, 420.0, 200.0, false,
                        2.5, 360, 640);
        check(!ev.reliable &&
                  std::fabs(ev.dist_cm - truth_d(35.0)) < 1e-9,
              "estimator: junk radius holds within T");
        // Without a model: analytic fallback drives the estimator.
        DistEstimator est2(2.0, 0.6, 0.25, 0.08, 45.0);
        ev = est2.update(70.0, 91.7, 40.0, 2800.0, 320.0, 180.0, false, 0.0,
                         360, 640);
        check(ev.reliable &&
                  std::fabs(ev.dist_cm -
                            analytic_distance_cm(91.7, 640.0, 45.0)) < 1e-9,
              "estimator without model uses the analytic fallback");
        ev = est2.update(std::nan(""), 91.7, 40.0, 2800.0, 320.0, 180.0,
                         false, 0.04, 360, 640);
        check(!ev.reliable && std::fabs(ev.dist_cm -
                                        analytic_distance_cm(91.7, 640.0,
                                                             45.0)) < 1e-9,
              "estimator: junk radius holds within T");
        DistEstimator est3(2.0, 0.6, 0.25, 0.08, 45.0);
        ev = est3.update(std::nan(""), 91.7, 40.0, 2800.0, 320.0, 180.0,
                         false, 0.04, 360, 640);
        check(!ev.reliable && std::isnan(ev.dist_cm),
              "estimator: junk radius with no hold -> nothing");
    }

    // 6. JSON round-trip + corrupt tolerance (contract file).
    {
        const std::string ts = iso_timestamp_now();
        const std::string text = distance_model_to_json(m, ts);
        DistanceModel back = distance_model_from_json(text);
        check(back.valid, "json round-trip keeps the model valid");
        check(back.a == m.a && back.b == m.b && back.c == m.c,
              "json round-trip is exact");
        check(back.points.size() == m.points.size(),
              "json round-trip keeps the points");
        DistanceModel broken = distance_model_from_json("{not json at all");
        check(!broken.valid, "corrupt json -> invalid model");
        DistanceModel wrong = distance_model_from_json(
            "{\"version\":9,\"a\":-1,\"b\":2,\"c\":3}");
        check(!wrong.valid, "wrong version rejected");
        DistanceModel nan_model = distance_model_from_json(
            "{\"version\":1,\"a\":NaN}");
        check(!nan_model.valid, "NaN a/b/c rejected");
        const std::string path =
            std::string(tmpdir) + "/distance_calib_test.json";
        check(save_distance_model(path, m, ts), "save_distance_model writes");
        DistanceModel loaded = load_distance_model(path);
        check(loaded.valid && loaded.a == m.a, "load(save(model)) round-trip");
        check(!load_distance_model(std::string(tmpdir) +
                                   "/distance_calib_missing.json")
                   .valid,
              "missing file -> invalid model");
    }
}

// Idea 6 parity driver (out/dist_check.py): the file contains
//   MODEL a b c frame_w frame_h     (a fitted model — Python writes repr()s)
//   PTS n + d r pairs               (calibration points of that model)
//   FIT npts + PT d r lines         (calibration points for a fresh fit)
//   EVAL min_dim r_px               (model probes)
//   EST hold drop fill move gate_w  (estimator params; LAST section)
//   STEP radius w h area cx cy held now min_dim frame_w   (until EOF)
// The MODEL section installs the model (with its r-interval from PTS) for
// the EVAL probes and the EST steps. C++ prints one line per request — the
// Python side must match bit-for-bit (IEEE-754 doubles through %.17g).
void test_distance_parity_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("FAIL: cannot open distance parity file %s\n", path);
        ++g_failures;
        return;
    }
    DistanceModel model;
    bool have_model = false;
    char kind[16];
    while (std::fscanf(f, "%15s", kind) == 1) {
        if (std::strcmp(kind, "MODEL") == 0) {
            double a, b, c;
            int fw, fh;
            if (std::fscanf(f, "%lf %lf %lf %d %d", &a, &b, &c, &fw, &fh) != 5) {
                std::printf("FAIL: malformed MODEL line\n");
                ++g_failures;
                break;
            }
            model = DistanceModel();
            model.a = a;
            model.b = b;
            model.c = c;
            model.frame_w = fw;
            model.frame_h = fh;
            // Optional PTS section: the calibration points of this model —
            // they define the r-interval exactly like a loaded file does.
            char sub[16];
            if (std::fscanf(f, "%15s", sub) == 1 &&
                std::strcmp(sub, "PTS") == 0) {
                int npts = 0;
                if (std::fscanf(f, "%d", &npts) != 1 || npts < 0) {
                    std::printf("FAIL: malformed PTS line\n");
                    ++g_failures;
                    break;
                }
                for (int i = 0; i < npts; ++i) {
                    double dv, rv;
                    if (std::fscanf(f, "%lf %lf", &dv, &rv) != 2) break;
                    model.points.push_back(dv);
                    model.points.push_back(rv);
                }
                if (model.points.size() >= 2) {
                    model.r_lo = model.r_hi = model.points[1];
                    for (size_t i = 1; i < model.points.size(); i += 2) {
                        model.r_lo = std::min(model.r_lo, model.points[i]);
                        model.r_hi = std::max(model.r_hi, model.points[i]);
                    }
                    model.has_interval = true;
                }
            }
            validate_distance_model(model);
            have_model = model.valid;
            std::printf("MODEL %.17g %.17g %.17g %d\n", model.a, model.b,
                        model.c, model.valid ? 1 : 0);
        } else if (std::strcmp(kind, "FIT") == 0) {
            int npts = 0;
            if (std::fscanf(f, "%d", &npts) != 1 || npts < 0) {
                std::printf("FAIL: malformed FIT line\n");
                ++g_failures;
                break;
            }
            std::vector<double> d, r;
            bool ok_in = true;
            for (int i = 0; i < npts; ++i) {
                double dv, rv;
                if (std::fscanf(f, "%lf %lf", &dv, &rv) != 2) {
                    ok_in = false;
                    break;
                }
                d.push_back(dv);
                r.push_back(rv);
            }
            if (!ok_in) {
                std::printf("FAIL: malformed PT lines\n");
                ++g_failures;
                break;
            }
            DistanceModel m;
            const bool ok = fit_distance_model(d, r, 640, 360, m);
            std::printf("FITTED %.17g %.17g %.17g %d\n", m.a, m.b, m.c,
                        ok && m.valid ? 1 : 0);
        } else if (std::strcmp(kind, "EVAL") == 0) {
            int min_dim;
            double r_px;
            if (std::fscanf(f, "%d %lf", &min_dim, &r_px) != 2) {
                std::printf("FAIL: malformed EVAL line\n");
                ++g_failures;
                break;
            }
            const DistEval ev =
                have_model ? eval_distance(model, r_px, min_dim)
                           : DistEval{std::nan(""), false};
            std::printf("EVAL %.17g %d\n", ev.dist_cm,
                        ev.reliable ? 1 : 0);
        } else if (std::strcmp(kind, "EST") == 0) {
            double hold, drop, fill, move, gw;
            if (std::fscanf(f, "%lf %lf %lf %lf %lf", &hold, &drop, &fill,
                            &move, &gw) != 5) {
                std::printf("FAIL: malformed EST line\n");
                ++g_failures;
                break;
            }
            DistEstimator est(hold, drop, fill, move, gw);
            if (have_model) est.set_model(model);
            // STEP lines run to EOF (the EST section is last by format).
            char sub[16];
            while (std::fscanf(f, "%15s", sub) == 1) {
                if (std::strcmp(sub, "STEP") != 0) {
                    std::printf("FAIL: unknown parity token %s\n", sub);
                    ++g_failures;
                    break;
                }
                double rad, w, h, area, cx, cy, now_s;
                int held, min_dim, fw;
                // File field order matches update(): held (int) before now_s.
                if (std::fscanf(f, "%lf %lf %lf %lf %lf %lf %d %lf %d %d",
                                &rad, &w, &h, &area, &cx, &cy,
                                &held, &now_s, &min_dim, &fw) != 10) {
                    std::printf("FAIL: malformed STEP line\n");
                    ++g_failures;
                    break;
                }
                const DistEval ev =
                    est.update(rad, w, h, area, cx, cy, held != 0, now_s,
                               min_dim, fw);
                std::printf("STEP %.17g %d\n", ev.dist_cm,
                            ev.reliable ? 1 : 0);
            }
        } else {
            std::printf("FAIL: unknown parity token %s\n", kind);
            ++g_failures;
            break;
        }
    }
    std::fclose(f);
}

int run_all(const char* tmpdir) {
    test_hsv_golden();
    test_gate_params();
    test_angle();
    test_serial_policy();
    test_color_message();
    test_gate_cluster();
    test_self_mask();
    test_distance(tmpdir);
    test_gate_store(tmpdir);
    test_atomic_file(tmpdir);
    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const char* tmpdir = std::getenv("PI_CORE_TEST_TMP");
    if (!tmpdir || tmpdir[0] == '\0') tmpdir = "/tmp";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--vectors") == 0 && i + 1 < argc) {
            test_hsv_vectors_file(argv[++i]);
            std::printf("checks=%d failures=%d\n", g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--cluster-parity") == 0 && i + 1 < argc) {
            // Idea 5 parity: Python (out/parity_check5.py) writes the scene,
            // this prints the C++ results for the driver to compare.
            test_cluster_parity_file(argv[++i]);
            std::printf("checks=%d failures=%d\n", g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--selfmask-parity") == 0 && i + 1 < argc) {
            // Idea 8 parity: Python (out/selfmask_check.py) writes cases and
            // probes, this prints the C++ keep-mask counts / point results.
            test_self_mask_parity_file(argv[++i]);
            std::printf("checks=%d failures=%d\n", g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--dist-parity") == 0 && i + 1 < argc) {
            // Idea 6 parity: Python (out/dist_check.py) writes a fitted
            // model, calibration points and estimator steps; this prints the
            // C++ results for the driver to compare bit-for-bit.
            test_distance_parity_file(argv[++i]);
            std::printf("checks=%d failures=%d\n", g_checks, g_failures);
            return g_failures == 0 ? 0 : 1;
        }
    }
    return run_all(tmpdir);
}
