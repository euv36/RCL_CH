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

    return 0;
}
