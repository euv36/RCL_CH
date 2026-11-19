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

bool preview_point_to_image(const PreviewRect& fitted, int rel_x, int rel_y,
                            int image_w, int image_h, int& out_x, int& out_y) {
    if (fitted.w <= 0 || fitted.h <= 0 || image_w <= 0 || image_h <= 0) return false;
    const int local_x = rel_x - fitted.x;
    const int local_y = rel_y - fitted.y;
    if (local_x < 0 || local_y < 0 || local_x >= fitted.w || local_y >= fitted.h) return false;
    out_x = static_cast<int>(std::lround(static_cast<double>(local_x) * image_w / fitted.w));
    out_y = static_cast<int>(std::lround(static_cast<double>(local_y) * image_h / fitted.h));
    if (out_x < 0) out_x = 0;
    if (out_y < 0) out_y = 0;
    if (out_x >= image_w) out_x = image_w - 1;
    if (out_y >= image_h) out_y = image_h - 1;
    return true;
}

} // namespace camera_cpp::laptop
