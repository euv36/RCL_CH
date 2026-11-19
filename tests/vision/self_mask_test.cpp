#include "vision/self_mask.hpp"

#include <cmath>

#include "../test_checks.hpp"

using camera_cpp::SelfMask;
using camera_cpp::build_keep_mask;

int main() {
    const cv::Size size(100, 100);

    auto disabled = build_keep_mask(size, SelfMask{false, 0.5F, 0.5F, 0.2F, 0.0F});
    CAMERA_TEST_CHECK(disabled.type() == CV_8UC1);
    CAMERA_TEST_CHECK(disabled.at<unsigned char>(50, 50) == 255);
    CAMERA_TEST_CHECK(cv::countNonZero(disabled) == 10000);

    auto circle = build_keep_mask(size, SelfMask{true, 0.5F, 0.5F, 0.2F, 0.0F});
    CAMERA_TEST_CHECK(circle.at<unsigned char>(50, 50) == 0);
    CAMERA_TEST_CHECK(circle.at<unsigned char>(20, 50) == 255);
    CAMERA_TEST_CHECK(circle.at<unsigned char>(0, 0) == 255);

    auto annulus = build_keep_mask(size, SelfMask{true, 0.5F, 0.5F, 0.1F, 0.3F});
    CAMERA_TEST_CHECK(annulus.at<unsigned char>(50, 50) == 0);
    CAMERA_TEST_CHECK(annulus.at<unsigned char>(30, 50) == 255);
    CAMERA_TEST_CHECK(annulus.at<unsigned char>(0, 50) == 0);

    auto malformed = build_keep_mask(size, SelfMask{true, 2.0F, -1.0F, NAN, -1.0F});
    CAMERA_TEST_CHECK(malformed.at<unsigned char>(50, 50) == 0);
    CAMERA_TEST_CHECK(malformed.at<unsigned char>(0, 0) == 255);
    CAMERA_TEST_CHECK(cv::countNonZero(malformed) > 0);

    // The keep mask is consumed by cv::bitwise_and; an antialiased grey rim
    // would survive into connected components and leak robot-body pixels.
    auto require_binary = [](const cv::Mat& m) {
        for (int y = 0; y < m.rows; ++y) {
            const auto* row = m.ptr<unsigned char>(y);
            for (int x = 0; x < m.cols; ++x) {
                CAMERA_TEST_CHECK(row[x] == 0 || row[x] == 255);
            }
        }
    };
    require_binary(disabled);
    require_binary(circle);
    require_binary(annulus);
    require_binary(malformed);

    // Fractions must be resolution-independent: cx/cy scale with width/height,
    // r/r2 with min(width, height) (legacy params_from_fractions contract).
    auto wide = build_keep_mask(cv::Size(200, 100), SelfMask{true, 0.5F, 0.5F, 0.2F, 0.0F});
    CAMERA_TEST_CHECK(wide.at<unsigned char>(50, 100) == 0);
    CAMERA_TEST_CHECK(wide.at<unsigned char>(50, 90) == 0);
    CAMERA_TEST_CHECK(wide.at<unsigned char>(50, 20) == 255);
    auto tall = build_keep_mask(cv::Size(100, 200), SelfMask{true, 0.5F, 0.5F, 0.2F, 0.0F});
    CAMERA_TEST_CHECK(tall.at<unsigned char>(100, 50) == 0);
    CAMERA_TEST_CHECK(tall.at<unsigned char>(20, 50) == 255);
    auto wide_ann = build_keep_mask(cv::Size(200, 100), SelfMask{true, 0.5F, 0.5F, 0.1F, 0.3F});
    CAMERA_TEST_CHECK(wide_ann.at<unsigned char>(50, 120) == 255);
    CAMERA_TEST_CHECK(wide_ann.at<unsigned char>(50, 140) == 0);

    return 0;
}
