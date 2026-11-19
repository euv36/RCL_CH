#include "laptop/panels.hpp"

#include "../test_checks.hpp"

using camera_cpp::laptop::compute_ui_layout;
using camera_cpp::laptop::UiLayout;
using camera_cpp::laptop::UiRect;

namespace {

bool inside(const UiRect& r, float w, float h) {
    return r.x >= 0.0F && r.y >= 0.0F && r.w > 0.0F && r.h > 0.0F &&
           r.x + r.w <= w + 0.01F && r.y + r.h <= h + 0.01F;
}

bool overlaps(const UiRect& a, const UiRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

int main() {
    const UiLayout layout = compute_ui_layout(1280.0F, 720.0F);

    CAMERA_TEST_CHECK(inside(layout.top_row, 1280.0F, 720.0F));
    CAMERA_TEST_CHECK(inside(layout.left_video, 1280.0F, 720.0F));
    CAMERA_TEST_CHECK(inside(layout.left_mask, 1280.0F, 720.0F));
    CAMERA_TEST_CHECK(inside(layout.right_source, 1280.0F, 720.0F));
    CAMERA_TEST_CHECK(inside(layout.bottom_controls, 1280.0F, 720.0F));
    CAMERA_TEST_CHECK(inside(layout.gates_panel, 1280.0F, 720.0F));

    CAMERA_TEST_CHECK(layout.top_row.y == 0.0F);
    CAMERA_TEST_CHECK(layout.top_row.h >= 30.0F);
    CAMERA_TEST_CHECK(!overlaps(layout.left_video, layout.left_mask));
    CAMERA_TEST_CHECK(!overlaps(layout.left_video, layout.right_source));
    CAMERA_TEST_CHECK(!overlaps(layout.bottom_controls, layout.right_source));
    CAMERA_TEST_CHECK(layout.right_source.x > layout.left_mask.x);
    CAMERA_TEST_CHECK(layout.bottom_controls.y > layout.top_row.y + layout.top_row.h);
    return 0;
}
