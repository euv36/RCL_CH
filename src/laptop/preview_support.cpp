#include "laptop/preview_support.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace camera_cpp::laptop {

RgbaImage bgr_to_rgba(const cv::Mat& bgr) {
    RgbaImage result;
    if (bgr.empty() || bgr.type() != CV_8UC3) return result;

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    result.width = rgb.cols;
    result.height = rgb.rows;
    result.pixels.reserve(static_cast<std::size_t>(rgb.cols) * rgb.rows * 4U);
    for (int y = 0; y < rgb.rows; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            result.pixels.push_back(row[x][0]);
            result.pixels.push_back(row[x][1]);
            result.pixels.push_back(row[x][2]);
            result.pixels.push_back(255U);
        }
    }
    return result;
}

PreviewRect fit_rect(int window_w, int window_h, int image_w, int image_h) {
    PreviewRect result;
    if (window_w <= 0 || window_h <= 0 || image_w <= 0 || image_h <= 0) return result;

    const double scale = std::min(static_cast<double>(window_w) / image_w,
                                  static_cast<double>(window_h) / image_h);
    result.w = std::min(window_w, static_cast<int>(std::lround(image_w * scale)));
    result.h = std::min(window_h, static_cast<int>(std::lround(image_h * scale)));
    result.x = (window_w - result.w) / 2;
    result.y = (window_h - result.h) / 2;
    return result;
}

} // namespace camera_cpp::laptop
