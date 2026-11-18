#include "pi_camera/v4l2_capture.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace camera_cpp {
bool normalize_capture_config(CaptureConfig& config, std::string* error, std::string* warning) {
    if (config.width <= 0 || config.height <= 0 || config.fps <= 0) {
        if (error) *error = "width, height and fps must be positive";
        return false;
    }

    std::string fourcc = config.fourcc;
    std::transform(fourcc.begin(), fourcc.end(), fourcc.begin(), [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    if (fourcc != "MJPG") {
        if (warning) *warning = "capture fourcc should normally be MJPG on HD USB Camera";
    }
    config.fourcc = fourcc;

    if (config.device.empty()) {
        config.device = "/dev/video0";
    }
    if (error) error->clear();
    if (warning && fourcc == "MJPG") warning->clear();
    return true;
}

namespace {
class OpenCvBackend final : public CaptureBackend {
public:
    bool open(const CaptureConfig& config, std::string* error) override {
        capture_.release();
        capture_.open(config.device, cv::CAP_V4L2);
        if (!capture_.isOpened()) {
            if (error) *error = "OpenCV could not open " + config.device;
            return false;
        }

        const int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        capture_.set(cv::CAP_PROP_FOURCC, static_cast<double>(fourcc));
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(config.width));
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(config.height));
        capture_.set(cv::CAP_PROP_FPS, static_cast<double>(config.fps));
        return true;
    }

    bool read(cv::Mat& frame, std::string* error) override {
        if (!capture_.isOpened()) {
            if (error) *error = "capture is not opened";
            return false;
        }
        if (!capture_.read(frame) || frame.empty()) {
            if (error) *error = "failed to capture frame";
            return false;
        }
        return true;
    }

    void close() override {
        capture_.release();
    }

private:
    cv::VideoCapture capture_;
};
}

std::unique_ptr<CaptureBackend> make_open_cv_v4l2_backend() {
    return std::make_unique<OpenCvBackend>();
}

struct V4l2Capture::Impl {
    std::unique_ptr<CaptureBackend> backend;
    CaptureConfig config{};
    bool opened = false;
};

V4l2Capture::V4l2Capture() : impl_(std::make_unique<Impl>()) {
    impl_->backend = make_open_cv_v4l2_backend();
}

V4l2Capture::V4l2Capture(std::unique_ptr<CaptureBackend> backend) : impl_(std::make_unique<Impl>()) {
    impl_->backend = backend ? std::move(backend) : make_open_cv_v4l2_backend();
}

V4l2Capture::~V4l2Capture() = default;

bool V4l2Capture::open(const CaptureConfig& config) {
    std::string error;
    auto normalized = config;
    if (!normalize_capture_config(normalized, &error)) return false;
    close();
    if (!impl_->backend->open(normalized, &error)) return false;
    impl_->config = normalized;
    impl_->opened = true;
    return true;
}

std::optional<cv::Mat> V4l2Capture::read() {
    if (!impl_->opened) return std::nullopt;
    cv::Mat frame;
    std::string error;
    if (!impl_->backend->read(frame, &error) || frame.empty()) return std::nullopt;
    return frame;
}

void V4l2Capture::close() {
    if (impl_->backend) impl_->backend->close();
    impl_->opened = false;
}

cv::Mat downscale_and_rotate(const cv::Mat& full_frame) {
    if (full_frame.empty()) return {};
    cv::Mat processing;
    cv::resize(full_frame, processing, {640, 360}, 0.0, 0.0, cv::INTER_AREA);
    return processing;
}

} // namespace camera_cpp
