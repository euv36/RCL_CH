#pragma once

#include <opencv2/core.hpp>

namespace camera_cpp {

struct SelfMask {
    bool enabled = false;
    float cx = 0.5F;
    float cy = 0.5F;
    float r = 0.22F;
    float r2 = 0.0F;
};

cv::Mat build_keep_mask(cv::Size size, const SelfMask& mask);

} // namespace camera_cpp
