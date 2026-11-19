#include "theme.hpp"

namespace camera_cpp::laptop {

const Rgba PanelTheme::window_bg{0.07F, 0.08F, 0.10F, 1.0F};
const Rgba PanelTheme::accent{0.35F, 0.65F, 0.95F, 1.0F};
const Rgba PanelTheme::accent_hovered{0.45F, 0.74F, 1.0F, 1.0F};
const Rgba PanelTheme::accent_active{0.25F, 0.52F, 0.82F, 1.0F};
const Rgba PanelTheme::text{0.92F, 0.94F, 0.98F, 1.0F};
const Rgba PanelTheme::warning{1.0F, 0.4F, 0.4F, 1.0F};

// RGB triples mirrored from HsvPalette; rendered as fill colors in the UI.
const std::array<Rgba, 3> PanelTheme::gate_colors{{
    {1.0F, 1.0F, 0.0F, 1.0F},
    {0.0F, 0.502F, 1.0F, 1.0F},
    {1.0F, 0.502F, 0.0F, 1.0F},
}};

} // namespace camera_cpp::laptop
