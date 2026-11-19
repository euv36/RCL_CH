#pragma once

#include "self_mask.hpp"
#include "distance.hpp"
#include "gate_cluster.hpp"

#include <array>
#include <optional>

#include <opencv2/core.hpp>

namespace camera_cpp {

struct GateThreshold {
    int l_min = 0;
    int l_max = 255;
    int a_min = 0;
    int a_max = 255;
    int b_min = 0;
    int b_max = 255;
    bool configured = false;
};

struct GateTrackResult {
    bool enabled = false;
    bool found = false;
    bool held = false;
    float angle_deg = 0.0F;
    float radius_px = 0.0F;
    float dist_cm = 0.0F;
    bool dist_reliable = false;
    cv::Rect bbox{};
    cv::Mat mask;
};

struct TrackInput {
    const cv::Mat* portrait_rgb = nullptr;
    std::array<GateThreshold, 3> thresholds{};
    SelfMask self_mask{};
    std::array<std::optional<cv::Point2f>, 3> previous_positions{};
};

struct TrackStageTimes {
    float cvt_ms = 0.0F;
    float mask_ms = 0.0F;
    float cc_ms = 0.0F;
    float cluster_ms = 0.0F;
    float total_ms = 0.0F;
};

class TrackingEngine {
public:
    std::array<GateTrackResult, 3> track(const TrackInput& input);
    TrackStageTimes last_stage_ms() const noexcept;
    std::size_t cache_footprint_bytes() const noexcept;

private:
    cv::Mat lab_;
    cv::Mat keep_mask_;
    std::array<cv::Mat, 3> raw_masks_;
    std::array<cv::Mat, 3> filtered_masks_;
    std::array<cv::Mat, 3> component_masks_;
    TrackStageTimes last_times_;
    std::array<DistanceEstimator, 3> distance_estimators_{};
    std::array<GateStability, 3> stability_{};
    std::array<cv::Rect, 3> last_bbox_{};
    std::array<cv::Point2f, 3> last_position_{};
    std::array<bool, 3> has_last_position_{};
    std::array<float, 3> last_dist_cm_{};
};

std::array<GateTrackResult, 3> track_gates(TrackInput in);

} // namespace camera_cpp
