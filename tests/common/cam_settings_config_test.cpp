#include "cam_settings_config.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::CamSettingsConfig;

static std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

int main() {
    const auto path = temp_path("camera_cpp_cam_settings_test.json");
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << "{\"version\":1,\"controls\":{\"unknown_control\":123,\"exposure\":99999,\"gain\":-5,\"brightness\":1000,\"gamma\":1,\"saturation\":200,\"contrast\":100,\"power_line_frequency\":9,\"exposure_dynamic_framerate\":4}}";
    }

    CamSettingsConfig settings;
    CAMERA_TEST_CHECK(settings.load(path.string()));
    CAMERA_TEST_CHECK(settings.get("exposure") == 5000.0F);
    CAMERA_TEST_CHECK(settings.get("gain") == 0.0F);
    CAMERA_TEST_CHECK(settings.get("brightness") == 64.0F);
    CAMERA_TEST_CHECK(settings.get("gamma") == 72.0F);
    CAMERA_TEST_CHECK(settings.get("saturation") == 128.0F);
    CAMERA_TEST_CHECK(settings.get("contrast") == 64.0F);
    CAMERA_TEST_CHECK(settings.get("power_line_frequency") == 2.0F);
    CAMERA_TEST_CHECK(settings.get("exposure_dynamic_framerate") == 1.0F);
    settings.set("power_line_frequency", 7);
    CAMERA_TEST_CHECK(settings.get("power_line_frequency") == 2.0F);
    CAMERA_TEST_CHECK(settings.save());

    nlohmann::json after;
    {
        std::ifstream file(path, std::ios::binary);
        after = nlohmann::json::parse(file);
    }
    CAMERA_TEST_CHECK(after.at("controls").at("unknown_control") == 123);
    CAMERA_TEST_CHECK(after.at("controls").at("exposure") == 5000);

    std::filesystem::remove(path);
    return 0;
}
