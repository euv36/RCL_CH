#pragma once

#include <memory>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace camera_cpp {

struct CaptureConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string fourcc = "MJPG";
    std::string device = "/dev/video0";
};

struct CaptureOpenReport {
    bool ok = false;
    std::string warning;
    std::string error;
};

bool normalize_capture_config(CaptureConfig& config, std::string* error, std::string* warning = nullptr);

class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual bool open(const CaptureConfig& config, std::string* error) = 0;
    virtual bool read(cv::Mat& frame, std::string* error) = 0;
    virtual void close() = 0;
};

std::unique_ptr<CaptureBackend> make_open_cv_v4l2_backend();

class V4l2Capture {
public:
    V4l2Capture();
    explicit V4l2Capture(std::unique_ptr<CaptureBackend> backend);
    ~V4l2Capture();

    bool open(const CaptureConfig& config = {});
    std::optional<cv::Mat> read();
    void close();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

cv::Mat downscale_and_rotate(const cv::Mat& bgr_frame);

} // namespace camera_cpp
