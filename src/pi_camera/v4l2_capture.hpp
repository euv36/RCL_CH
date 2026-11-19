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

// Values actually negotiated by the capture device, as opposed to requested.
struct CaptureProperties {
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string fourcc;
};

bool normalize_capture_config(CaptureConfig& config, std::string* error, std::string* warning = nullptr);

class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual bool open(const CaptureConfig& config, std::string* error) = 0;
    virtual bool read(cv::Mat& frame, std::string* error) = 0;
    // Property readback contract: backends must report what the device negotiated.
    virtual bool properties(CaptureProperties& properties, std::string* error) const = 0;
    virtual void close() = 0;
};

std::unique_ptr<CaptureBackend> make_open_cv_v4l2_backend();

class V4l2Capture {
public:
    V4l2Capture();
    explicit V4l2Capture(std::unique_ptr<CaptureBackend> backend);
    ~V4l2Capture();

    bool open(const CaptureConfig& config = {}, CaptureOpenReport* report = nullptr);
    // Config with negotiated values filled in after a successful open().
    const CaptureConfig& negotiated_config() const;
    std::optional<cv::Mat> read();
    void close();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Возвращает новый буфер кадра 640x360 (полный FOV без crop).
cv::Mat downscale_and_rotate(const cv::Mat& bgr_frame);
// Переиспользует вызванным кодом буфер между кадрами: память выделяется только
// при первом вызове или при изменении размера/типа входного кадра.
void downscale_and_rotate(const cv::Mat& bgr_frame, cv::Mat& processing);

} // namespace camera_cpp
