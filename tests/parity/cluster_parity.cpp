#include "../test_checks.hpp"
#include "vision/gate_cluster.hpp"

#include <cmath>
#include <vector>

int main() {
    const GateClusterParams params = resolve_cluster_params(720, 1280);
    const std::vector<GateComponent> split = {
        {300, 520, 85, 150, 12750, 342.5f, 595.0f},
        {398, 520, 85, 150, 12750, 440.5f, 595.0f},
    };
    const std::vector<GateComponent> folder_tape = {
        {420, 520, 190, 150, 28500, 515.0f, 595.0f},
        {120, 880, 360, 11, 3960, 300.0f, 885.5f},
    };
    const std::vector<GateComponent> lamp = {
        {40, 40, 80, 700, 56000, 80.0f, 390.0f},
    };

    const GateObject folder = build_gate_object(folder_tape, params, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(folder.found);
    CAMERA_TEST_CHECK(std::abs(folder.cx - 515.0f) < 1e-5f);
    CAMERA_TEST_CHECK(std::abs(folder.cy - 595.0f) < 1e-5f);
    CAMERA_TEST_CHECK(std::abs(folder.area - 28500.0f) < 1e-3f);
    CAMERA_TEST_CHECK(folder.members == 1);

    const GateObject split_gate = build_gate_object(split, params, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(split_gate.found);
    CAMERA_TEST_CHECK(std::abs(split_gate.cx - 391.5f) < 1e-5f);
    CAMERA_TEST_CHECK(split_gate.members == 2);
    CAMERA_TEST_CHECK(split_gate.bbox.w == 183.0f);
    CAMERA_TEST_CHECK(split_gate.bbox.h == 150.0f);

    const GateObject rejected_lamp = build_gate_object(lamp, params, std::nullopt, std::nullopt);
    CAMERA_TEST_CHECK(!rejected_lamp.found);

    GateStability stability(3, 5);
    const GateStabilityResult first = stability.update(true, 515.0f, 595.0f, 121.0f, 28500.0f, 1);
    CAMERA_TEST_CHECK(first.found);
    for (int i = 0; i < 4; ++i) {
        const GateStabilityResult held = stability.update(false, 0.0f, 0.0f, 0.0f, 0.0f, 0);
        CAMERA_TEST_CHECK(held.found);
        CAMERA_TEST_CHECK(held.held);
        CAMERA_TEST_CHECK(!held.lost_event);
    }
    const GateStabilityResult lost = stability.update(false, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    CAMERA_TEST_CHECK(!lost.found);
    CAMERA_TEST_CHECK(lost.lost_event);
    return 0;
}
