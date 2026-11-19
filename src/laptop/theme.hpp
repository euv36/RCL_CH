#pragma once

#include <array>

namespace camera_cpp::laptop {

struct Rgba {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

struct PanelTheme {
    static const Rgba window_bg;
    static const Rgba accent;
    static const Rgba accent_hovered;
    static const Rgba accent_active;
    static const Rgba text;
    static const Rgba warning;
    static const std::array<Rgba, 3> gate_colors;
};

} // namespace camera_cpp::laptop
