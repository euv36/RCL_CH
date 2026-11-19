#include "pi_camera/camera_engine.hpp"
#include "pi_camera/v4l2_capture.hpp"

#include <memory>
#include <string>

#include <opencv2/imgproc.hpp>

#include "../test_checks.hpp"

using camera_cpp::CameraEngine;
using camera_cpp::CaptureBackend;
using camera_cpp::CaptureConfig;
using camera_cpp::CaptureOpenReport;
using camera_cpp::CaptureProperties;
using camera_cpp::V4l2Capture;
using camera_cpp::downscale_and_rotate;
using camera_cpp::normalize_capture_config;

namespace {
struct FakeBackend final : CaptureBackend {
    cv::Mat synthetic;
    bool opened = false;
    bool expose_properties = true;
    CaptureProperties negotiated{1920, 1080, 30, "MJPG"};
    CaptureConfig requested{};

    explicit FakeBackend(int width = 1920, int height = 1080) {
        synthetic = cv::Mat(height, width, CV_8UC3);
        for (int y = 0; y < synthetic.rows; y += 20) {
            cv::rectangle(synthetic, {0, y}, {synthetic.cols - 1, y + 10},
                cv::Scalar((y * 7) % 255, 20, 220 - (y * 3) % 255), cv::FILLED);
        }
        negotiated.width = width;
        negotiated.height = height;
    }

    bool open(const CaptureConfig& config, std::string* error) override {
        if (error) error->clear();
        requested = config;
        opened = true;
        return true;
    }
    bool read(cv::Mat& frame, std::string* error) override {
        if (error) error->clear();
        if (!opened) {
            if (error) *error = "fake capture is not opened";
            return false;
        }
        frame = synthetic.clone();
        return true;
    }
    bool properties(CaptureProperties& out, std::string* error) const override {
        if (error) error->clear();
        if (!opened) {
            if (error) *error = "fake capture is not opened";
            return false;
        }
        if (!expose_properties) {
            if (error) *error = "fake backend has no property contract";
            return false;
        }
        out = negotiated;
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
    CAMERA_TEST_CHECK(non_mjpg.fourcc == "YUYV");
    CAMERA_TEST_CHECK(!warning.empty());

    CaptureConfig malformed_fourcc;
    malformed_fourcc.fourcc = "MJPEG";
    error.clear();
    warning.clear();
    CAMERA_TEST_CHECK(!normalize_capture_config(malformed_fourcc, &error, &warning));
    CAMERA_TEST_CHECK(!error.empty());

    CaptureConfig defaults;
    defaults.device.clear();
    error.clear();
    warning.clear();
    CAMERA_TEST_CHECK(normalize_capture_config(defaults, &error, &warning));
    CAMERA_TEST_CHECK(defaults.device == "/dev/video0");
    CAMERA_TEST_CHECK(warning.empty());

    cv::Mat source(1080, 1920, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::rectangle(source, {960, 540}, {1919, 1079}, cv::Scalar(200, 100, 0), cv::FILLED);
    auto processed = downscale_and_rotate(source);
    CAMERA_TEST_CHECK(processed.size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(processed.type() == CV_8UC3);
    CAMERA_TEST_CHECK(processed.at<cv::Vec3b>(270, 480)[0] > processed.at<cv::Vec3b>(270, 100)[0]);

    // Fake backend: requested MJPG/1920x1080/30 is confirmed by property readback.
    auto backend = std::make_unique<FakeBackend>();
    auto* fake = backend.get();
    V4l2Capture capture(std::move(backend));
    CaptureOpenReport report;
    CAMERA_TEST_CHECK(capture.open({}, &report));
    CAMERA_TEST_CHECK(report.ok);
    CAMERA_TEST_CHECK(report.error.empty());
    CAMERA_TEST_CHECK(report.warning.empty());
    CAMERA_TEST_CHECK(fake->requested.device == "/dev/video0");
    CAMERA_TEST_CHECK(fake->requested.fourcc == "MJPG");
    CAMERA_TEST_CHECK(capture.negotiated_config().width == 1920);
    CAMERA_TEST_CHECK(capture.negotiated_config().height == 1080);
    CAMERA_TEST_CHECK(capture.negotiated_config().fps == 30);
    CAMERA_TEST_CHECK(capture.negotiated_config().fourcc == "MJPG");
    auto frame = capture.read();
    CAMERA_TEST_CHECK(frame.has_value());
    CAMERA_TEST_CHECK(frame->size() == cv::Size(1920, 1080));
    capture.close();
    CAMERA_TEST_CHECK(!capture.read().has_value());

    // Explicit non-MJPG: the warning is truthful because the backend gets the requested FOURCC.
    auto yuyv_backend = std::make_unique<FakeBackend>();
    yuyv_backend->negotiated.fourcc = "YUYV";
    auto* yuyv_fake = yuyv_backend.get();
    CaptureConfig explicit_yuyv;
    explicit_yuyv.fourcc = "YUYV";
    V4l2Capture yuyv_capture(std::move(yuyv_backend));
    CaptureOpenReport yuyv_report;
    CAMERA_TEST_CHECK(yuyv_capture.open(explicit_yuyv, &yuyv_report));
    CAMERA_TEST_CHECK(yuyv_report.ok && yuyv_report.error.empty());
    CAMERA_TEST_CHECK(yuyv_report.warning.find("MJPG") != std::string::npos);
    CAMERA_TEST_CHECK(yuyv_report.warning.find("negotiated") == std::string::npos);
    CAMERA_TEST_CHECK(yuyv_fake->requested.fourcc == "YUYV");
    CAMERA_TEST_CHECK(yuyv_capture.negotiated_config().fourcc == "YUYV");

    // Mismatch: open proceeds, but the warning and EngineFrame report the real values.
    auto small_backend = std::make_unique<FakeBackend>(1280, 720);
    small_backend->negotiated.fps = 24;
    CameraEngine engine(std::move(small_backend));
    CaptureOpenReport small_report;
    CAMERA_TEST_CHECK(engine.open({}, &small_report));
    CAMERA_TEST_CHECK(small_report.ok);
    CAMERA_TEST_CHECK(small_report.warning.find("1280x720") != std::string::npos);
    CAMERA_TEST_CHECK(small_report.warning.find("1920x1080") != std::string::npos);
    auto engine_frame = engine.read();
    CAMERA_TEST_CHECK(engine_frame.has_value());
    CAMERA_TEST_CHECK(engine_frame->full_frame.size() == cv::Size(1280, 720));
    CAMERA_TEST_CHECK(engine_frame->processing_frame.size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(engine_frame->negotiated.width == 1280);
    CAMERA_TEST_CHECK(engine_frame->negotiated.height == 720);
    CAMERA_TEST_CHECK(engine_frame->negotiated.fps == 24);
    CAMERA_TEST_CHECK(engine_frame->negotiated.device == "/dev/video0");
    CAMERA_TEST_CHECK(engine_frame->negotiated.fourcc == "MJPG");

    // Missing property contract is an honest error, not a silent success.
    auto no_props_backend = std::make_unique<FakeBackend>();
    no_props_backend->expose_properties = false;
    V4l2Capture no_props(std::move(no_props_backend));
    CaptureOpenReport no_props_report;
    CAMERA_TEST_CHECK(!no_props.open({}, &no_props_report));
    CAMERA_TEST_CHECK(!no_props_report.ok);
    CAMERA_TEST_CHECK(no_props_report.error.find("verify") != std::string::npos);
    CAMERA_TEST_CHECK(!no_props.read().has_value());

    // FPS may be unreported; the fallback is disclosed in the warning.
    auto no_fps_backend = std::make_unique<FakeBackend>();
    no_fps_backend->negotiated.fps = 0;
    V4l2Capture no_fps(std::move(no_fps_backend));
    CaptureOpenReport no_fps_report;
    CAMERA_TEST_CHECK(no_fps.open({}, &no_fps_report));
    CAMERA_TEST_CHECK(no_fps_report.warning.find("did not report FPS") != std::string::npos);
    CAMERA_TEST_CHECK(no_fps.negotiated_config().fps == 30);

    // Повторное чтение через read_reusing обязано переиспользовать буфер
    // обработанного кадра: закреплённый Mat-заголовок удерживает старую память,
    // поэтому новая аллокация изменила бы указатель данных.
    auto reuse_backend = std::make_unique<FakeBackend>();
    CameraEngine reuse_engine(std::move(reuse_backend));
    CAMERA_TEST_CHECK(reuse_engine.open({}, nullptr));
    camera_cpp::EngineFrame reuse_frame;
    CAMERA_TEST_CHECK(reuse_engine.read_reusing(reuse_frame));
    cv::Mat pinned_processing = reuse_frame.processing_frame;
    CAMERA_TEST_CHECK(reuse_engine.read_reusing(reuse_frame));
    CAMERA_TEST_CHECK(reuse_frame.processing_frame.data == pinned_processing.data);
    CAMERA_TEST_CHECK(reuse_frame.processing_frame.size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(reuse_engine.read_reusing(reuse_frame));
    CAMERA_TEST_CHECK(reuse_frame.processing_frame.data == pinned_processing.data);

    return 0;
}
