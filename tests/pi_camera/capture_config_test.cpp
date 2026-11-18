#include "pi_camera/v4l2_capture.hpp"

#include <memory>
#include <string>

#include <opencv2/imgproc.hpp>

#include "../test_checks.hpp"

using camera_cpp::CaptureBackend;
using camera_cpp::CaptureConfig;
using camera_cpp::V4l2Capture;
using camera_cpp::downscale_and_rotate;
using camera_cpp::normalize_capture_config;

namespace {
struct FakeBackend final : CaptureBackend {
    cv::Mat synthetic;
    bool opened = false;
    bool open(const CaptureConfig&, std::string* error) override {
        if (error) error->clear();
        synthetic = cv::Mat(1080, 1920, CV_8UC3);
        for (int y = 0; y < synthetic.rows; y += 20) {
            cv::rectangle(synthetic, {0, y}, {1919, y + 10}, cv::Scalar((y * 7) % 255, 20, 220 - (y * 3) % 255), cv::FILLED);
        }
        opened = true;
        return true;
    }
    bool read(cv::Mat& frame, std::string* error) override {
        if (error) error->clear();
        if (!opened) return false;
        frame = synthetic.clone();
        return true;
    }
    void close() override { opened = false; }
};
}

int main() {
    CaptureConfig empty_device;
    empty_device.device.clear();
    empty_device.width = 0;
    std::string error;
    std::string warning;
    CAMERA_TEST_CHECK(!normalize_capture_config(empty_device, &error, &warning));
    CAMERA_TEST_CHECK(!error.empty());

    CaptureConfig non_mjpg;
    non_mjpg.fourcc = "YUYV";
    error.clear();
    warning.clear();
    CAMERA_TEST_CHECK(normalize_capture_config(non_mjpg, &error, &warning));
    CAMERA_TEST_CHECK(!warning.empty());

    CaptureConfig defaults;
    defaults.device.clear();
    error.clear();
    warning.clear();
    CAMERA_TEST_CHECK(normalize_capture_config(defaults, &error, &warning));
    CAMERA_TEST_CHECK(defaults.device == "/dev/video0");

    cv::Mat source(1080, 1920, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::rectangle(source, {960, 540}, {1919, 1079}, cv::Scalar(200, 100, 0), cv::FILLED);
    auto processed = downscale_and_rotate(source);
    CAMERA_TEST_CHECK(processed.size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(processed.type() == CV_8UC3);
    CAMERA_TEST_CHECK(processed.at<cv::Vec3b>(270, 480)[0] > processed.at<cv::Vec3b>(270, 100)[0]);

    auto backend = std::make_unique<FakeBackend>();
    V4l2Capture capture(std::move(backend));
    CAMERA_TEST_CHECK(capture.open({}));
    auto frame = capture.read();
    CAMERA_TEST_CHECK(frame.has_value());
    CAMERA_TEST_CHECK(frame->size() == cv::Size(1920, 1080));
    capture.close();
    CAMERA_TEST_CHECK(!capture.read().has_value());

    return 0;
}
