#include "state.hpp"

#include <cstdio>

namespace camera_cpp::laptop {

AppState::AppState() {
    std::snprintf(source_buffer, sizeof(source_buffer), "%s", camera_source_text.c_str());
    for (std::size_t i = 0; i < radar_gates.size(); ++i) {
        radar_gates[i].id = static_cast<int>(i) + 1;
        radar_gates[i].rgb = gate_colors[i];
    }
}

} // namespace camera_cpp::laptop
