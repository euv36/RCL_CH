#include "laptop/preview_support.hpp"

#include "../test_checks.hpp"

#include <cstdint>

using camera_cpp::laptop::RgbaImage;
using camera_cpp::laptop::bgr_to_rgba;
using camera_cpp::laptop::fit_rect;

int main() {
    cv::Mat bgr(1, 2, CV_8UC3);
    bgr.at<cv::Vec3b>(0, 0) = cv::Vec3b(10, 20, 30);  // B, G, R
    bgr.at<cv::Vec3b>(0, 1) = cv::Vec3b(40, 50, 60);

    const RgbaImage image = bgr_to_rgba(bgr);
    CAMERA_TEST_CHECK(image.width == 2);
    CAMERA_TEST_CHECK(image.height == 1);
    CAMERA_TEST_CHECK(image.pixels.size() == 8U);
    CAMERA_TEST_CHECK(image.pixels[0] == 30U);
    CAMERA_TEST_CHECK(image.pixels[1] == 20U);
    CAMERA_TEST_CHECK(image.pixels[2] == 10U);
    CAMERA_TEST_CHECK(image.pixels[3] == 255U);
    CAMERA_TEST_CHECK(image.pixels[4] == 60U);
    CAMERA_TEST_CHECK(image.pixels[6] == 40U);

    const RgbaImage empty = bgr_to_rgba(cv::Mat{});
    CAMERA_TEST_CHECK(empty.width == 0);
    CAMERA_TEST_CHECK(empty.height == 0);
    CAMERA_TEST_CHECK(empty.pixels.empty());

    const camera_cpp::laptop::PreviewRect wide = fit_rect(800, 600, 720, 1280);
    CAMERA_TEST_CHECK(wide.w > 0 && wide.h > 0);
    CAMERA_TEST_CHECK(wide.x >= 0 && wide.y >= 0);
    CAMERA_TEST_CHECK(wide.x + wide.w <= 800);
    CAMERA_TEST_CHECK(wide.y + wide.h <= 600);
    CAMERA_TEST_CHECK(wide.h == 600);
    CAMERA_TEST_CHECK(wide.w > 330 && wide.w < 345);

    const camera_cpp::laptop::PreviewRect tall = fit_rect(400, 800, 720, 1280);
    CAMERA_TEST_CHECK(tall.w > 0 && tall.h > 0);
    CAMERA_TEST_CHECK(tall.x >= 0 && tall.y >= 0);
    CAMERA_TEST_CHECK(tall.x + tall.w <= 400);
    CAMERA_TEST_CHECK(tall.y + tall.h <= 800);
    CAMERA_TEST_CHECK(tall.w == 400);
    return 0;
}
