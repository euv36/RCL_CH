#pragma once

#include <array>

namespace camera_cpp::laptop {

struct RadarGate {
    int id = 1;
    bool enabled = false;
    bool found = false;
    float angle_deg = 0.0F;
    float distance_cm = 0.0F;
    std::array<int, 3> rgb{{255, 255, 255}};
};

bool radar_gate_visible(const RadarGate& gate);

} // namespace camera_cpp::laptop
