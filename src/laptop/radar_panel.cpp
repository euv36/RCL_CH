#include "radar_panel.hpp"

namespace camera_cpp::laptop {

bool radar_gate_visible(const RadarGate& gate) {
    // Local configuration is authoritative: a stale Pi "found" flag must never
    // draw a gate that is not enabled in saved_gates.json.
    return gate.enabled && gate.found;
}

} // namespace camera_cpp::laptop
