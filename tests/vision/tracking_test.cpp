#include "vision/tracking.hpp"

#include <cmath>

#include <opencv2/imgproc.hpp>

#include "../test_checks.hpp"

using camera_cpp::GateThreshold;
using camera_cpp::TrackInput;
using camera_cpp::TrackingEngine;
using camera_cpp::track_gates;

namespace {

cv::Vec3b lab_for_rgb(cv::Vec3b rgb) {
    cv::Mat pixel(1, 1, CV_8UC3, rgb);
    cv::cvtColor(pixel, pixel, cv::COLOR_RGB2Lab);
    return pixel.at<cv::Vec3b>(0, 0);
}

GateThreshold threshold_for_rgb(cv::Vec3b rgb) {
    const cv::Vec3b lab = lab_for_rgb(rgb);
    GateThreshold threshold;
    threshold.l_min = std::max(0, static_cast<int>(lab[0]) - 30);
    threshold.l_max = std::min(255, static_cast<int>(lab[0]) + 30);
    threshold.a_min = std::max(0, static_cast<int>(lab[1]) - 40);
    threshold.a_max = std::min(255, static_cast<int>(lab[1]) + 40);
    threshold.b_min = std::max(0, static_cast<int>(lab[2]) - 40);
    threshold.b_max = std::min(255, static_cast<int>(lab[2]) + 40);
    threshold.configured = true;
    return threshold;
}

cv::Mat synthetic_yellow_gate() {
    cv::Mat image(720, 1280, CV_8UC3, cv::Scalar(25, 25, 30));
    cv::rectangle(image, cv::Rect(900, 285, 110, 150), cv::Scalar(255, 255, 0), cv::FILLED);
    return image;
}

} // namespace

int main() {
    TrackInput empty;
    auto none = track_gates(empty);
    for (const auto& result : none) {
        CAMERA_TEST_CHECK(!result.enabled);
        CAMERA_TEST_CHECK(!result.found);
        CAMERA_TEST_CHECK(!result.held);
        CAMERA_TEST_CHECK(result.mask.empty());
    }

    cv::Mat image = synthetic_yellow_gate();
    TrackInput input;
    input.portrait_rgb = &image;
    input.thresholds[0] = threshold_for_rgb(cv::Vec3b(255, 255, 0));

    auto results = track_gates(input);
    CAMERA_TEST_CHECK(results[0].enabled);
    CAMERA_TEST_CHECK(results[0].found);
    CAMERA_TEST_CHECK(std::abs(results[0].angle_deg) < 1.0F);
    CAMERA_TEST_CHECK(results[0].bbox.area() > 0);
    CAMERA_TEST_CHECK(results[0].bbox.x >= 880 && results[0].bbox.x <= 920);
    CAMERA_TEST_CHECK(results[0].bbox.width >= 95 && results[0].bbox.width <= 125);
    CAMERA_TEST_CHECK(!results[0].mask.empty());
    CAMERA_TEST_CHECK(results[0].mask.size() == image.size());
    CAMERA_TEST_CHECK(results[0].mask.at<unsigned char>(375, 955) != 0);
    CAMERA_TEST_CHECK(results[0].mask.at<unsigned char>(10, 10) == 0);
    CAMERA_TEST_CHECK(!results[1].enabled);
    CAMERA_TEST_CHECK(!results[2].enabled);

    TrackingEngine engine;
    std::size_t previous_footprint = 0;
    for (int i = 0; i < 100; ++i) {
        auto cached = engine.track(input);
        CAMERA_TEST_CHECK(cached[0].enabled);
        const std::size_t footprint = engine.cache_footprint_bytes();
        CAMERA_TEST_CHECK(footprint >= previous_footprint);
        previous_footprint = footprint;
    }
    const auto timings = engine.last_stage_ms();
    CAMERA_TEST_CHECK(timings.cvt_ms >= 0.0F);
    CAMERA_TEST_CHECK(timings.mask_ms >= 0.0F);
    CAMERA_TEST_CHECK(timings.cc_ms >= 0.0F);
    CAMERA_TEST_CHECK(timings.cluster_ms >= 0.0F);
    CAMERA_TEST_CHECK(timings.total_ms >= 0.0F);

    return 0;
}
