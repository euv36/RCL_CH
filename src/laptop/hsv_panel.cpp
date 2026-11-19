#include "hsv_panel.hpp"

#include <opencv2/imgproc.hpp>

namespace camera_cpp::laptop {
namespace {

cv::Mat normalized_frame(const cv::Mat& frame_rgb) {
    if (frame_rgb.empty() || frame_rgb.type() != CV_8UC3 || frame_rgb.dims != 2) {
        return cv::Mat(720, 1280, CV_8UC3, cv::Scalar::all(0));
    }
    return frame_rgb;
}

} // namespace

cv::Mat build_hsv_mask_bgra(const cv::Mat& frame_rgb,
                            const std::array<cv::Mat, 3>& masks,
                            const HsvPalette& palette) {
    const cv::Mat frame = normalized_frame(frame_rgb);

    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_RGB2HSV);
    cv::Mat bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);

    cv::Mat out_f;
    bgr.convertTo(out_f, CV_32FC3);

    const cv::Mat kernel = cv::Mat::ones(3, 3, CV_8UC1);
    for (std::size_t i = 0; i < masks.size(); ++i) {
        const cv::Mat& mask = masks[i];
        if (mask.empty() || mask.type() != CV_8UC1 || mask.size() != frame.size()) {
            continue;
        }
        if (cv::countNonZero(mask) == 0) {
            continue;
        }

        const cv::Vec3b& rgb = palette.gate_colors[i];
        const cv::Vec3f color(static_cast<float>(rgb[2]),
                              static_cast<float>(rgb[1]),
                              static_cast<float>(rgb[0]));

        cv::Mat eroded;
        cv::erode(mask, eroded, kernel, cv::Point(-1, -1), 1);

        for (int y = 0; y < frame.rows; ++y) {
            const unsigned char* mrow = mask.ptr<unsigned char>(y);
            const unsigned char* erow = eroded.ptr<unsigned char>(y);
            cv::Vec3f* orow = out_f.ptr<cv::Vec3f>(y);
            for (int x = 0; x < frame.cols; ++x) {
                if (mrow[x] == 0) {
                    continue;
                }
                orow[x] = erow[x] != 0
                    ? orow[x] * 0.55F + color * 0.45F
                    : color;
            }
        }
    }

    cv::Mat out_u8;
    out_f.convertTo(out_u8, CV_8UC3);

    std::vector<cv::Mat> channels;
    cv::split(out_u8, channels);
    channels.push_back(cv::Mat(frame.size(), CV_8UC1, cv::Scalar::all(255)));
    cv::Mat bgra;
    cv::merge(channels, bgra);
    return bgra;
}

} // namespace camera_cpp::laptop
