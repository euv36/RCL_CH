#include "pi_camera/status_builder.hpp"

#include <cmath>

namespace camera_cpp {

nlohmann::json StatusBuilder::build(const std::array<GateStatus, 3>& gates,
                                    float fps,
                                    std::array<int, 2> frame_size,
                                    std::string_view source) {
    nlohmann::json gates_json = nlohmann::json::object();
    for (const auto& gate : gates) {
        const std::string key = std::to_string(gate.id);
        gates_json[key] = {
            {"id", gate.id},
            {"enabled", gate.enabled},
            {"found", gate.found},
            {"held", gate.held},
            {"angle", gate.angle_deg},
            {"r_px", gate.radius_px},
            {"dist_cm", gate.dist_cm},
            {"dist_reliable", gate.dist_reliable},
        };
    }

    return {
        {"fps", fps},
        {"src", std::string(source)},
        {"frame_wh", {frame_size[0], frame_size[1]}},
        {"gates", gates_json},
    };
}

} // namespace camera_cpp
