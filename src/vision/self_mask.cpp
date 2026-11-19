#include "vision/self_mask.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace camera_cpp {
namespace {

float unit_fraction(float value, float fallback) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F ? value : fallback;
}

} // namespace

cv::Mat build_keep_mask(cv::Size size, const SelfMask& mask) {
    const int width = std::max(size.width, 1);
    const int height = std::max(size.height, 1);

    if (!mask.enabled) {
        return cv::Mat(height, width, CV_8UC1, cv::Scalar(255));
    }

    const float min_side = static_cast<float>(std::min(width, height));
    const float cx = unit_fraction(mask.cx, 0.5F) * static_cast<float>(width);
    const float cy = unit_fraction(mask.cy, 0.5F) * static_cast<float>(height);
    const float radius_frac = std::isfinite(mask.r) && mask.r > 0.0F ? mask.r : 0.22F;
    const float radius = radius_frac * min_side;
    if (!std::isfinite(radius) || radius <= 0.0F) {
        return cv::Mat(height, width, CV_8UC1, cv::Scalar(255));
    }

    float outer_radius = 0.0F;
    if (std::isfinite(mask.r2) && mask.r2 > radius_frac) {
        outer_radius = mask.r2 * min_side;
    }

    cv::Mat out(height, width, CV_8UC1, cv::Scalar(0));
    const cv::Point center(cvRound(cx), cvRound(cy));
    // Keep mask must stay strictly binary (0/255), matching the legacy
    // equation d2 > r^2 (and d2 < r2^2 for an annulus). cv::LINE_AA leaves a
    // grey rim; bitwise_and would preserve those partial values and let
    // robot-body pixels survive into connected components.
    if (outer_radius > 0.0F) {
        // Exclusion modeled as an annulus: center is unusable (robot body),
        // outside the outer radius is unusable too. Mid-annulus remains visible.
        cv::circle(out, center, cvRound(outer_radius), cv::Scalar(255), cv::FILLED, cv::LINE_8);
        cv::circle(out, center, cvRound(radius), cv::Scalar(0), cv::FILLED, cv::LINE_8);
    } else {
        cv::circle(out, center, cvRound(radius), cv::Scalar(255), cv::FILLED, cv::LINE_8);
        out = 255 - out;
    }
    return out;
}

} // namespace camera_cpp
