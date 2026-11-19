// Тест чистых helpers приложения: GUI в ctest не открывается.
#include <opencv2/core.hpp>

#include <string>

#include "../test_checks.hpp"
#include "laptop/cam_settings_panel.hpp"
#include "laptop/gate_publisher.hpp"
#include "laptop/opencv_app.hpp"
#include "laptop/source_controller.hpp"

int main() {
    camera_cpp::laptop::OpenCVApp app;
    CAMERA_TEST_CHECK(!app.has_frame());

    const cv::Mat rgb(360, 640, CV_8UC3, cv::Scalar(30, 60, 90));
    app.process_frame(rgb);
    CAMERA_TEST_CHECK(app.has_frame());
    CAMERA_TEST_CHECK(app.last_frame_size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(app.frames_processed() == 1);

    app.state().camera_source_text.clear();
    app.append_source_text("10.0.0.1");
    CAMERA_TEST_CHECK(app.source_input() == "10.0.0.1");

    const auto request = camera_cpp::laptop::parse_connect("10.0.0.1");
    CAMERA_TEST_CHECK(request.valid());
    CAMERA_TEST_CHECK(request.kind == camera_cpp::laptop::AppState::SourceKind::Pi);
    CAMERA_TEST_CHECK(request.host == "10.0.0.1");
    CAMERA_TEST_CHECK(request.port == 5555);

    // Дополнительные окна строятся из синтетических данных без GUI.
    app.state().radar_gates[0].found = true;
    app.state().radar_gates[0].angle_deg = 30.0F;
    app.state().radar_gates[0].distance_cm = 120.0F;
    CAMERA_TEST_CHECK(!app.build_radar_frame().empty());
    CAMERA_TEST_CHECK(!app.build_hsv_frame().empty());
    CAMERA_TEST_CHECK(!app.build_help_frame().empty());

    CAMERA_TEST_CHECK(std::string(camera_cpp::laptop::cam_apply_status_text(
        camera_cpp::laptop::CamApplyStatus::Applied)).size() > 0);
    CAMERA_TEST_CHECK(std::string(camera_cpp::laptop::cam_apply_status_text(
        camera_cpp::laptop::CamApplyStatus::Idle)).size() > 0);

    // Роль unset не добавляет поле в payload 5557.
    const auto payload = camera_cpp::laptop::build_gate_payload(
        {{1, {10, 20, 30}, "unset"}});
    CAMERA_TEST_CHECK(payload.find("role") == std::string::npos);

    return 0;
}
