#include "pi_camera/status_builder.hpp"

#include <algorithm>
#include <cmath>

namespace camera_cpp {
namespace {

// Legacy wire contract uses 0 degrees to the right, CCW, wrapped into [0, 360).
float wrap_angle(float degrees) {
    if (!std::isfinite(degrees)) {
        return 0.0F;
    }
    float wrapped = std::fmod(degrees, 360.0F);
    if (wrapped < 0.0F) {
        wrapped += 360.0F;
    }
    return wrapped >= 360.0F ? 0.0F : wrapped;
}

float non_negative(float value) {
    return std::isfinite(value) ? std::max(0.0F, value) : 0.0F;
}

} // namespace

// Собирает статус для планировщика в формате legacy wire-контракта.
// Отключённые ворота не публикуют found/held/площадь/расстояние.
nlohmann::json StatusBuilder::build(const std::array<GateStatus, 3>& gates,
                                    float fps,
                                    std::array<int, 2> frame_size,
                                    std::string_view source) {
    nlohmann::json gates_json = nlohmann::json::object();
    for (const auto& gate : gates) {
        const std::string key = std::to_string(gate.id);
        // A disabled gate must never claim detection, hold, area or distance.
        const bool enabled = gate.enabled;
        const bool found = enabled && gate.found;
        const bool held = enabled && gate.held;
        const bool reliable = enabled && gate.dist_reliable;
        const float area = enabled ? non_negative(gate.area_px) : 0.0F;
        const float radius = enabled ? non_negative(gate.radius_px) : 0.0F;
        const float distance = enabled ? non_negative(gate.dist_cm) : 0.0F;
        gates_json[key] = {
            {"id", gate.id},
            {"enabled", enabled},
            {"found", found},
            {"held", held},
            {"role", gate.role.empty() ? "unset" : gate.role},
            {"angle", wrap_angle(gate.angle_deg)},
            {"r_px", radius},
            {"area", area},
            {"area_px", area},
            {"dist_cm", distance},
            {"dist_reliable", reliable},
        };
    }

    return {
        {"fps", std::isfinite(fps) ? std::max(0.0F, fps) : 0.0F},
        {"src", std::string(source)},
        {"frame_wh", {frame_size[0], frame_size[1]}},
        {"gates", gates_json},
    };
}

} // namespace camera_cpp
