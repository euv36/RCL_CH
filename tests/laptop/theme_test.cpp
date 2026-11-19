#include "laptop/theme.hpp"

#include "../test_checks.hpp"

using camera_cpp::laptop::PanelTheme;
using camera_cpp::laptop::Rgba;

namespace {

bool opaque_unit(const Rgba& c) {
    return c.a == 1.0F && c.r >= 0.0F && c.r <= 1.0F &&
           c.g >= 0.0F && c.g <= 1.0F &&
           c.b >= 0.0F && c.b <= 1.0F;
}

} // namespace

int main() {
    CAMERA_TEST_CHECK(opaque_unit(PanelTheme::window_bg));
    CAMERA_TEST_CHECK(opaque_unit(PanelTheme::accent));
    CAMERA_TEST_CHECK(opaque_unit(PanelTheme::text));
    CAMERA_TEST_CHECK(opaque_unit(PanelTheme::warning));

    // Distinct hues, not default-constructed black.
    CAMERA_TEST_CHECK(PanelTheme::window_bg.r < 0.2F);
    CAMERA_TEST_CHECK(PanelTheme::accent.b > 0.8F);
    CAMERA_TEST_CHECK(PanelTheme::accent.r < PanelTheme::accent.b);
    CAMERA_TEST_CHECK(PanelTheme::text.r > 0.8F);
    CAMERA_TEST_CHECK(PanelTheme::warning.r > 0.8F && PanelTheme::warning.g < 0.5F);

    CAMERA_TEST_CHECK(PanelTheme::gate_colors.size() == 3);
    for (const Rgba& color : PanelTheme::gate_colors) {
        CAMERA_TEST_CHECK(opaque_unit(color));
        CAMERA_TEST_CHECK(color.r + color.g + color.b > 0.5F);
    }
    CAMERA_TEST_CHECK(PanelTheme::gate_colors[0].r > 0.5F && PanelTheme::gate_colors[0].g > 0.5F);
    return 0;
}
