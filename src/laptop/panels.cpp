#include "panels.hpp"

namespace camera_cpp::laptop {

UiLayout compute_ui_layout(float width, float height) {
    UiLayout layout;
    const float margin = 8.0F;
    const float gap = 8.0F;
    const float top_h = 36.0F;
    const float bottom_h = 72.0F;

    layout.top_row = {0.0F, 0.0F, width, top_h};

    const float content_top = top_h + gap;
    const float content_bottom = height - bottom_h - gap;
    const float row_h = (content_bottom - content_top - gap) * 0.5F;
    const float second_y = content_top + row_h + gap;

    const float left_w = width * 0.59F;
    const float right_x = margin + left_w + gap;
    const float right_w = width - right_x - margin;

    layout.left_video = {margin, content_top, left_w, row_h};
    layout.left_mask = {margin, second_y, left_w, content_bottom - second_y};
    layout.right_source = {right_x, content_top, right_w, row_h};
    layout.gates_panel = {right_x, second_y, right_w, content_bottom - second_y};
    layout.bottom_controls = {0.0F, height - bottom_h, width, bottom_h};

    return layout;
}

} // namespace camera_cpp::laptop
