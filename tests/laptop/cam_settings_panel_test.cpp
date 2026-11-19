#include "laptop/cam_settings_panel.hpp"

#include <string>

#include "../test_checks.hpp"

using camera_cpp::laptop::cam_apply_status_text;
using camera_cpp::laptop::cam_control_is_user_editable;
using camera_cpp::laptop::CamApplyStatus;

int main() {
    const char* applied = cam_apply_status_text(CamApplyStatus::Applied);
    const char* rejected = cam_apply_status_text(CamApplyStatus::Rejected);
    const char* offline = cam_apply_status_text(CamApplyStatus::Offline);
    CAMERA_TEST_CHECK(applied != nullptr && *applied != '\0');
    CAMERA_TEST_CHECK(rejected != nullptr && *rejected != '\0');
    CAMERA_TEST_CHECK(offline != nullptr && *offline != '\0');

    const std::string ok_text = applied;
    CAMERA_TEST_CHECK(ok_text.find("Применено") != std::string::npos);
    CAMERA_TEST_CHECK(std::string(rejected).find("Применено") == std::string::npos);
    CAMERA_TEST_CHECK(std::string(offline).find("Применено") == std::string::npos);
    CAMERA_TEST_CHECK(std::string(offline).find("Pi") != std::string::npos);

    // Auto-exposure is forced manual by the Pi apply path; the UI must never
    // offer it as an editable slider, and unknown keys are not exposed.
    CAMERA_TEST_CHECK(!cam_control_is_user_editable("auto_exposure"));
    CAMERA_TEST_CHECK(!cam_control_is_user_editable("unknown_control"));
    CAMERA_TEST_CHECK(cam_control_is_user_editable("exposure"));
    CAMERA_TEST_CHECK(cam_control_is_user_editable("brightness"));
    return 0;
}
