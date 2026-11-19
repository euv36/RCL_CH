#pragma once

#include <array>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

namespace camera_cpp {

struct GateStatus {
    int id = 0;
    bool enabled = false;
    bool found = false;
    float angle_deg = 0.0F;
    float radius_px = 0.0F;
    float dist_cm = 0.0F;
    bool dist_reliable = true;
    bool held = false;
    float area_px = 0.0F;
    std::string role = "unset";
};

class StatusBuilder {
public:
    static nlohmann::json build(const std::array<GateStatus, 3>& gates,
                                float fps,
                                std::array<int, 2> frame_size,
                                std::string_view source);
};

} // namespace camera_cpp
