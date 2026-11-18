#include "tracking.hpp"

#include <chrono>
#include <cmath>
#include <limits>

#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include "gate_cluster.hpp"

namespace camera_cpp {
namespace {

float elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

cv::Vec3b threshold_lower(const GateThreshold& threshold) {
    return cv::Vec3b(static_cast<unsigned char>(threshold.l_min),
                     static_cast<unsigned char>(threshold.a_min),
                     static_cast<unsigned char>(threshold.b_min));
}

cv::Vec3b threshold_upper(const GateThreshold& threshold) {
    return cv::Vec3b(static_cast<unsigned char>(threshold.l_max),
                     static_cast<unsigned char>(threshold.a_max),
                     static_cast<unsigned char>(threshold.b_max));
}

const std::array<cv::Vec3b, 3> kTrackColors{
    cv::Vec3b{255, 255, 0},
    cv::Vec3b{0, 128, 255},
    cv::Vec3b{255, 128, 0},
};

} // namespace

std::array<GateTrackResult, 3> TrackingEngine::track(const TrackInput& input) {
    const auto total_start = std::chrono::steady_clock::now();
    TrackStageTimes times{};
    std::array<GateTrackResult, 3> results{};
    last_times_ = {};

    bool any_configured = false;
    for (const auto& threshold : input.thresholds) {
        any_configured = any_configured || threshold.configured;
    }

    if (input.portrait_rgb == nullptr || input.portrait_rgb->empty() || input.portrait_rgb->channels() != 3) {
        last_times_ = times;
        return results;
    }

    const auto cvt_start = std::chrono::steady_clock::now();
    cv::cvtColor(*input.portrait_rgb, lab_, cv::COLOR_RGB2Lab);
    const auto cvt_end = std::chrono::steady_clock::now();
    times.cvt_ms = elapsed_ms(cvt_start, cvt_end);

    const cv::Size size = input.portrait_rgb->size();
    cv::Mat keep;
    if (input.self_mask.enabled) {
        keep_mask_ = build_keep_mask(size, input.self_mask);
        keep = keep_mask_;
    } else {
        keep_mask_.release();
    }

    std::array<cv::Mat, 3> labels;
    std::array<cv::Mat, 3> stats;
    std::array<cv::Mat, 3> centroids;

    const auto mask_start = std::chrono::steady_clock::now();
    for (std::size_t slot = 0; slot < results.size(); ++slot) {
        results[slot].enabled = input.thresholds[slot].configured;
        auto& mask = raw_masks_[slot];
        if (!results[slot].enabled) {
            mask.release();
            continue;
        }
        cv::inRange(lab_, threshold_lower(input.thresholds[slot]), threshold_upper(input.thresholds[slot]), mask);
        filtered_masks_[slot] = mask;
        if (!keep.empty()) {
            cv::bitwise_and(filtered_masks_[slot], keep, filtered_masks_[slot]);
        }
    }
    const auto mask_end = std::chrono::steady_clock::now();
    times.mask_ms = elapsed_ms(mask_start, mask_end);

    const auto cc_start = std::chrono::steady_clock::now();
    for (std::size_t slot = 0; slot < results.size(); ++slot) {
        if (!results[slot].enabled) {
            continue;
        }
        const int count = cv::connectedComponentsWithStats(filtered_masks_[slot],
                                                           labels[slot],
                                                           stats[slot],
                                                           centroids[slot],
                                                           8,
                                                           CV_32S);
        (void)count;
    }
    const auto cc_end = std::chrono::steady_clock::now();
    times.cc_ms = elapsed_ms(cc_start, cc_end);

    const auto cluster_start = std::chrono::steady_clock::now();
    const cv::Point2f frame_center(static_cast<float>(size.width - 1) * 0.5F,
                                   static_cast<float>(size.height - 1) * 0.5F);
    for (std::size_t slot = 0; slot < results.size(); ++slot) {
        if (!results[slot].enabled) {
            continue;
        }

        std::vector<GateComponent> components;
        const int rows = stats[slot].rows;
        components.reserve(static_cast<std::size_t>(std::max(0, rows - 1)));
        for (int label_index = 1; label_index < rows; ++label_index) {
            const int x = stats[slot].at<int>(label_index, cv::CC_STAT_LEFT);
            const int y = stats[slot].at<int>(label_index, cv::CC_STAT_TOP);
            const int w = stats[slot].at<int>(label_index, cv::CC_STAT_WIDTH);
            const int h = stats[slot].at<int>(label_index, cv::CC_STAT_HEIGHT);
            const int area = stats[slot].at<int>(label_index, cv::CC_STAT_AREA);
            if (area <= 0 || w <= 0 || h <= 0) {
                continue;
            }
            float center_x = 0.0F;
            float center_y = 0.0F;
            if (centroids[slot].depth() == CV_64F) {
                if (centroids[slot].channels() == 2) {
                    const cv::Vec2d center = centroids[slot].at<cv::Vec2d>(label_index);
                    center_x = static_cast<float>(center[0]);
                    center_y = static_cast<float>(center[1]);
                } else {
                    center_x = static_cast<float>(centroids[slot].at<double>(label_index, 0));
                    center_y = static_cast<float>(centroids[slot].at<double>(label_index, 1));
                }
            } else if (centroids[slot].depth() == CV_32F) {
                if (centroids[slot].channels() == 2) {
                    const cv::Vec2f center = centroids[slot].at<cv::Vec2f>(label_index);
                    center_x = center[0];
                    center_y = center[1];
                } else {
                    center_x = centroids[slot].at<float>(label_index, 0);
                    center_y = centroids[slot].at<float>(label_index, 1);
                }
            }
            components.push_back(GateComponent{x, y, w, h, area, center_x, center_y});
        }

        const auto params = resolve_cluster_params(size.width, size.height);
        const std::optional<float> previous_x = input.previous_positions[slot] ? std::optional<float>(input.previous_positions[slot]->x) : std::nullopt;
        const std::optional<float> previous_y = input.previous_positions[slot] ? std::optional<float>(input.previous_positions[slot]->y) : std::nullopt;
        const GateObject object = build_gate_object(components, params, previous_x, previous_y);
        if (!object.found) {
            continue;
        }

        auto& result = results[slot];
        result.found = true;
        result.angle_deg = std::atan2(frame_center.y - object.cy, object.cx - frame_center.x) *
                           (180.0F / static_cast<float>(kPi));
        result.radius_px = object.radius;
        result.bbox = (object.bbox.w > 0 && object.bbox.h > 0)
            ? cv::Rect(cvRound(object.bbox.x), cvRound(object.bbox.y), cvRound(object.bbox.w), cvRound(object.bbox.h))
            : cv::Rect();
        const float dx = object.cx - frame_center.x;
        const float dy = object.cy - frame_center.y;
        (void)dx;
        (void)dy;

        const float gate_width_cm = 45.0F;
        const DistResult distance = distance_estimators_[slot].update(object.radius,
                                                                      static_cast<float>(size.width),
                                                                      gate_width_cm);
        result.dist_cm = distance.cm;
        result.dist_reliable = distance.reliable;

        component_masks_[slot] = cv::Mat::zeros(size, CV_8UC1);
        if (!result.bbox.empty()) {
            cv::Mat region = filtered_masks_[slot](result.bbox);
            cv::Mat out_region = component_masks_[slot](result.bbox);
            region.copyTo(out_region);
        }
        result.mask = component_masks_[slot];
    }
    const auto cluster_end = std::chrono::steady_clock::now();
    times.cluster_ms = elapsed_ms(cluster_start, cluster_end);

    times.total_ms = elapsed_ms(total_start, std::chrono::steady_clock::now());
    last_times_ = times;
    (void)any_configured;
    return results;
}

TrackStageTimes TrackingEngine::last_stage_ms() const noexcept {
    return last_times_;
}

std::size_t TrackingEngine::cache_footprint_bytes() const noexcept {
    std::size_t bytes = 0;
    const auto add_mat = [&](const cv::Mat& mat) {
        if (!mat.empty()) {
            bytes += static_cast<std::size_t>(mat.rows) * static_cast<std::size_t>(mat.step);
        }
    };
    add_mat(lab_);
    add_mat(keep_mask_);
    for (const auto& mat : raw_masks_) add_mat(mat);
    for (const auto& mat : filtered_masks_) add_mat(mat);
    for (const auto& mat : component_masks_) add_mat(mat);
    return bytes;
}

std::array<GateTrackResult, 3> track_gates(TrackInput in) {
    static thread_local TrackingEngine engine;
    return engine.track(in);
}

} // namespace camera_cpp
