#pragma once

#include <optional>

#include <opencv2/core.hpp>

#include "pi_camera/v4l2_capture.hpp"

namespace camera_cpp {

struct EngineFrame {
    cv::Mat full_frame;
    cv::Mat processing_frame;
    CaptureConfig negotiated;
};

class CameraEngine {
public:
    explicit CameraEngine(std::unique_ptr<CaptureBackend> backend = make_open_cv_v4l2_backend());
    bool open(const CaptureConfig& config = {}, CaptureOpenReport* report = nullptr);
    std::optional<EngineFrame> read();
    void close();

private:
    V4l2Capture capture_;
    CaptureConfig negotiated_{};
};

} // namespace camera_cpp
