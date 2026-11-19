#pragma once

namespace camera_cpp::laptop {

struct UiRect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

struct UiLayout {
    UiRect top_row;
    UiRect left_video;
    UiRect left_mask;
    UiRect right_source;
    UiRect bottom_controls;
    UiRect gates_panel;
};

UiLayout compute_ui_layout(float width, float height);

} // namespace camera_cpp::laptop
