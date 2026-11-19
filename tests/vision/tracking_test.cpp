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

    // Legacy oracle (gate_cluster.angle_deg / pi_core::angle_from_center):
    // 0 deg = right, CCW, wrapped into [0, 360). In particular a gate below
    // the frame center must report ~270 deg, never -90 deg.
    const auto angle_for_gate_rect = [](cv::Rect rect) {
        cv::Mat scene(720, 1280, CV_8UC3, cv::Scalar(25, 25, 30));
        cv::rectangle(scene, rect, cv::Scalar(255, 255, 0), cv::FILLED);
        TrackInput scene_input;
        scene_input.portrait_rgb = &scene;
        scene_input.thresholds[0] = threshold_for_rgb(cv::Vec3b(255, 255, 0));
        // A fresh engine: the shared track_gates() engine keeps stability state
        // between calls, so unrelated scenes would be treated as a lost gate.
        TrackingEngine scene_engine;
        const float angle = scene_engine.track(scene_input)[0].angle_deg;
        CAMERA_TEST_CHECK(angle >= 0.0F && angle < 360.0F);
        return angle;
    };
    // Rectangles chosen so the connected-component centroid sits exactly on
    // the legacy integer frame center (640, 360) in x for up/down and in y
    // for left.
    CAMERA_TEST_CHECK(std::abs(angle_for_gate_rect(cv::Rect(585, 46, 111, 150)) - 90.0F) < 1.5F);
    CAMERA_TEST_CHECK(std::abs(angle_for_gate_rect(cv::Rect(265, 285, 111, 151)) - 180.0F) < 1.5F);
    CAMERA_TEST_CHECK(std::abs(angle_for_gate_rect(cv::Rect(585, 525, 111, 150)) - 270.0F) < 1.5F);

    // Stability must publish the last accepted position during the occlusion
    // grace period and drop it only after lost_frames empty frames.
    TrackingEngine hold_engine;
    TrackInput hold_input;
    hold_input.portrait_rgb = &image;
    hold_input.thresholds[0] = threshold_for_rgb(cv::Vec3b(255, 255, 0));
    const auto fresh = hold_engine.track(hold_input);
    CAMERA_TEST_CHECK(fresh[0].found);
    CAMERA_TEST_CHECK(!fresh[0].held);
    const float fresh_angle = fresh[0].angle_deg;
    const float fresh_radius = fresh[0].radius_px;

    cv::Mat blank(720, 1280, CV_8UC3, cv::Scalar(25, 25, 30));
    hold_input.portrait_rgb = &blank;
    for (int i = 0; i < 4; ++i) {
        const auto grace = hold_engine.track(hold_input);
        CAMERA_TEST_CHECK(grace[0].found);
        CAMERA_TEST_CHECK(grace[0].held);
        CAMERA_TEST_CHECK(std::abs(grace[0].angle_deg - fresh_angle) < 0.5F);
        CAMERA_TEST_CHECK(std::abs(grace[0].radius_px - fresh_radius) < 0.5F);
        CAMERA_TEST_CHECK(!grace[0].dist_reliable);
        CAMERA_TEST_CHECK(!grace[0].mask.empty());
    }
    const auto lost = hold_engine.track(hold_input);
    CAMERA_TEST_CHECK(!lost[0].found);
    CAMERA_TEST_CHECK(!lost[0].held);

    TrackingEngine engine;
    std::size_t previous_footprint = 0;
    for (int i = 0; i < 100; ++i) {
        auto cached = engine.track(input);
        CAMERA_TEST_CHECK(cached[0].enabled);
        const std::size_t footprint = engine.cache_footprint_bytes();
        // Buffers are allocated on the first frame; all later frames must
        // reuse them, so the cached footprint has to stay exactly constant.
        if (i > 0) {
            CAMERA_TEST_CHECK(footprint == previous_footprint);
        }
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
