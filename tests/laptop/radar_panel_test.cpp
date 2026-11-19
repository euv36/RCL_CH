#include "laptop/radar_panel.hpp"

#include "../test_checks.hpp"

using camera_cpp::laptop::RadarGate;
using camera_cpp::laptop::radar_gate_visible;

int main() {
    RadarGate gate;
    gate.id = 1;

    // Stale Pi status must not render a gate that is not enabled locally.
    gate.enabled = false;
    gate.found = true;
    CAMERA_TEST_CHECK(!radar_gate_visible(gate));

    // An enabled but currently unseen gate produces no blip either.
    gate.enabled = true;
    gate.found = false;
    CAMERA_TEST_CHECK(!radar_gate_visible(gate));

    // Only an enabled gate with a fresh positive detection is visible.
    gate.found = true;
    CAMERA_TEST_CHECK(radar_gate_visible(gate));
    return 0;
}
