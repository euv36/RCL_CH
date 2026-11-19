#pragma once

#include <array>

#include <opencv2/core.hpp>

namespace camera_cpp::laptop {

struct HsvPalette {
    // Gate colors are RGB, matching the saved gate colors in saved_gates.json.
    std::array<cv::Vec3b, 3> gate_colors{
        cv::Vec3b{255, 255, 0},
        cv::Vec3b{0, 128, 255},
        cv::Vec3b{255, 128, 0},
    };
    cv::Vec3b edge{255, 255, 255};
};

// Pure builder: HSV representation of frame_rgb with gate masks tinted in place.
// Returns CV_8UC4 with the same width/height as frame_rgb; alpha is always 255.
cv::Mat build_hsv_mask_bgra(const cv::Mat& frame_rgb,
                            const std::array<cv::Mat, 3>& masks,
                            const HsvPalette& palette);

} // namespace camera_cpp::laptop
