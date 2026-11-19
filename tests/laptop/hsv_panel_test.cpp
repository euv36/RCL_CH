#include "laptop/hsv_panel.hpp"

#include <array>

#include <opencv2/imgproc.hpp>

#include "../test_checks.hpp"

using camera_cpp::laptop::build_hsv_mask_bgra;

namespace {

cv::Mat rgb_color(int r, int g, int b) {
    // CV_8UC3 stores raw bytes; this builder interprets them as R,G,B.
    return cv::Mat(8, 8, CV_8UC3, cv::Vec3b(
        static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b)));
}

} // namespace

int main() {
    camera_cpp::laptop::HsvPalette palette;
    std::array<cv::Mat, 3> empty_masks;

    const cv::Mat empty = build_hsv_mask_bgra(cv::Mat(), empty_masks, palette);
    CAMERA_TEST_CHECK(empty.size() == cv::Size(1280, 720));
    CAMERA_TEST_CHECK(empty.type() == CV_8UC4);

    const cv::Mat frame = rgb_color(255, 0, 0);
    const cv::Mat plain = build_hsv_mask_bgra(frame, empty_masks, palette);
    CAMERA_TEST_CHECK(plain.size() == frame.size());
    CAMERA_TEST_CHECK(plain.type() == CV_8UC4);
    const cv::Vec4b base = plain.at<cv::Vec4b>(4, 4);
    CAMERA_TEST_CHECK(std::abs(static_cast<int>(base[0]) - 0) <= 2);
    CAMERA_TEST_CHECK(std::abs(static_cast<int>(base[1]) - 0) <= 2);
    CAMERA_TEST_CHECK(std::abs(static_cast<int>(base[2]) - 255) <= 2);
    CAMERA_TEST_CHECK(base[3] == 255);

    // A one-pixel mask must be tinted exactly at its own pixel, with no shift.
    auto masks = empty_masks;
    masks[0] = cv::Mat::zeros(frame.size(), CV_8UC1);
    masks[0].at<unsigned char>(3, 3) = 255;
    const cv::Mat tinted = build_hsv_mask_bgra(frame, masks, palette);
    const cv::Vec4b edge = tinted.at<cv::Vec4b>(3, 3);
    CAMERA_TEST_CHECK(edge[0] == 0);
    CAMERA_TEST_CHECK(edge[1] == 255);
    CAMERA_TEST_CHECK(edge[2] == 255);
    CAMERA_TEST_CHECK(edge[3] == 255);
    CAMERA_TEST_CHECK(tinted.at<cv::Vec4b>(3, 4) == base);

    // A mask with the wrong size is ignored rather than shifting the tint plane.
    masks[0] = cv::Mat::ones(4, 4, CV_8UC1);
    const cv::Mat ignored = build_hsv_mask_bgra(frame, masks, palette);
    CAMERA_TEST_CHECK(ignored.at<cv::Vec4b>(4, 4) == base);

    // Letterboxing is done by the caller; the builder must preserve aspect only.
    const cv::Mat wide = rgb_color(0, 255, 0);
    const cv::Mat wide_out = build_hsv_mask_bgra(wide, empty_masks, palette);
    CAMERA_TEST_CHECK(wide_out.size() == wide.size());
    return 0;
}
