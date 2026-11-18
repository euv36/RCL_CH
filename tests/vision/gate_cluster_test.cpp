#include "../test_checks.hpp"
#include "gate_cluster.hpp"
#include <cmath>
#include <iostream>
#include <vector>

namespace {
GateComponent comp(int x, int y, int w, int h) {
    GateComponent c{};
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.area = w * h;
    c.cx = x + w * 0.5f;
    c.cy = y + h * 0.5f;
    return c;
}

GateClusterParams params() {
    GateClusterParams p{};
    p.merge_gap_frac = 0.025f;
    p.max_stray_frac = 0.10f;
    p.open_kernel = 3;
    p.lost_frames = 5;
    p.confirm_frames = 3;
    p.max_candidates = 256;
    p.max_aspect = 6.5f;
    p.min_cluster_side_frac = 0.008f;
    return p;
}
}

int main() {
    const auto p = params();

    const auto folder = build_gate_object({comp(420, 520, 190, 150)}, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(folder.found);
    CAMERA_TEST_CHECK(folder.members == 1);
    CAMERA_TEST_CHECK(std::fabs(folder.cx - 515.0f) < 0.6f);
    CAMERA_TEST_CHECK(std::fabs(folder.cy - 595.0f) < 0.6f);

    const auto lamp = build_gate_object({comp(40, 40, 80, 700)}, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(!lamp.found);

    const auto tape = build_gate_object({comp(120, 880, 360, 11)}, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(!tape.found);

    const auto edge = build_gate_object({comp(0, 500, 105, 170)}, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(edge.found);
    CAMERA_TEST_CHECK(std::fabs(edge.cx - 52.5f) < 0.6f);

    const auto split = build_gate_object({comp(300, 520, 85, 150), comp(398, 520, 85, 150)}, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(split.found);
    CAMERA_TEST_CHECK(split.members == 2);
    CAMERA_TEST_CHECK(split.bbox.x == 300);
    CAMERA_TEST_CHECK(split.bbox.w == 183);
    CAMERA_TEST_CHECK(std::fabs(split.cx - 391.5f) < 0.6f);

    std::vector<GateComponent> many;
    many.reserve(300);
    for (int i = 0; i < 300; ++i) many.push_back(comp(100 + (i % 20) * 2, 100 + (i % 15) * 2, 90, 90));
    const auto capped = build_gate_object(many, p, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(capped.found);

    GateStability stability(p.confirm_frames, p.lost_frames, 128.0f);
    GateStabilityResult state = stability.update(true, 500.0f, 600.0f, 90.0f, 28500.0f, 1);
    CAMERA_TEST_CHECK(state.found);
    CAMERA_TEST_CHECK(!state.held);

    for (int i = 0; i < p.lost_frames - 1; ++i) {
        state = stability.update(false, 0.0f, 0.0f, 0.0f, 0.0f, 0);
        CAMERA_TEST_CHECK(state.found);
        CAMERA_TEST_CHECK(state.held);
    }
    state = stability.update(false, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    CAMERA_TEST_CHECK(!state.found);
    CAMERA_TEST_CHECK(state.lost_event);

    std::cout << "gate_cluster_test passed\n";
    return 0;
}
