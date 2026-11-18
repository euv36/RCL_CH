#include "../test_checks.hpp"
#include "hsv_color.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
bool approx(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void check_lab(uint8_t r, uint8_t g, uint8_t b, float l, float a, float yellow_b) {
    const auto lab = rgb_to_lab_u8(r, g, b);
    CAMERA_TEST_CHECK(approx(lab.L, l, 2.0f));
    CAMERA_TEST_CHECK(approx(lab.A, a, 2.0f));
    CAMERA_TEST_CHECK(approx(lab.B, yellow_b, 2.0f));
}
}

int main() {
    check_lab(0, 0, 0, 0.0f, 0.0f, 0.0f);
    check_lab(255, 255, 255, 100.0f, 0.0f, 0.0f);
    check_lab(0, 255, 0, 87.0f, -86.0f, 83.0f);
    check_lab(255, 0, 0, 53.0f, 80.0f, 67.0f);
    check_lab(255, 255, 0, 97.0f, -22.0f, 95.0f);
    check_lab(0, 0, 255, 32.0f, 79.0f, -108.0f);
    check_lab(255, 128, 0, 67.0f, 43.0f, 74.0f);

    const LabRange green_range{80.0f, 95.0f, -100.0f, -70.0f, 65.0f, 100.0f};
    const auto green = rgb_to_lab_u8(0, 255, 0);
    CAMERA_TEST_CHECK(lab_in_range(green, green_range));
    CAMERA_TEST_CHECK(!lab_in_range(green, LabRange{0.0f, 100.0f, -70.0f, 127.0f, -128.0f, 127.0f}));

    const auto orange = rgb_to_lab_u8(255, 128, 0);
    CAMERA_TEST_CHECK(!lab_in_range(orange, LabRange{0.0f, 100.0f, -100.0f, -70.0f, 65.0f, 100.0f}));

    const auto back = lab_to_rgb_u8(53.0f, 80.0f, 67.0f);
    CAMERA_TEST_CHECK(approx(back.L, 255.0f, 8.0f));
    CAMERA_TEST_CHECK(approx(back.A, 2.0f, 12.0f));
    CAMERA_TEST_CHECK(approx(back.B, 1.0f, 12.0f));

    std::cout << "hsv_color_test passed\n";
    return 0;
}
