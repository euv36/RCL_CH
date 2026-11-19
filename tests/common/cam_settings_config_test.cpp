#include "cam_settings_config.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::CamSettingsConfig;

namespace {

std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return nlohmann::json::parse(file);
}

bool is_iso_local_timestamp(const nlohmann::json& value) {
    if (!value.is_string()) return false;
    const std::string text = value.get<std::string>();
    if (text.size() != 19) return false;
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16) {
            continue;
        }
        if (text[index] < '0' || text[index] > '9') return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path paths[] = {
        temp_path("camera_cpp_cam_settings_clamp_test.json"),
        temp_path("camera_cpp_cam_settings_defaults_test.json"),
        temp_path("camera_cpp_cam_settings_unknown_test.json"),
        temp_path("camera_cpp_cam_settings_stale_a.json"),
        temp_path("camera_cpp_cam_settings_stale_b.json"),
        temp_path("camera_cpp_cam_settings_missing.json"),
        temp_path("camera_cpp_cam_settings_ts_test.json"),
    };
    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
    const auto& clamp_path = paths[0];
    const auto& defaults_path = paths[1];
    const auto& stale_a_path = paths[3];
    const auto& stale_b_path = paths[4];
    const auto& missing_path = paths[5];
    const auto& ts_path = paths[6];

    // One canonical table: name, min, max (mirrors the legacy V4L2 ranges).
    const std::vector<std::pair<std::string, std::pair<int, int>>> expected_ranges = {
        {"exposure", {1, 5000}},
        {"gain", {0, 100}},
        {"brightness", {-64, 64}},
        {"gamma", {72, 500}},
        {"saturation", {0, 128}},
        {"contrast", {0, 64}},
        {"power_line_frequency", {0, 2}},
        {"exposure_dynamic_framerate", {0, 1}},
    };
    const auto ranges = CamSettingsConfig::ranges();
    CAMERA_TEST_CHECK(ranges.size() == expected_ranges.size());
    for (std::size_t index = 0; index < expected_ranges.size(); ++index) {
        CAMERA_TEST_CHECK(ranges[index].first == expected_ranges[index].first);
        CAMERA_TEST_CHECK(ranges[index].second.first == expected_ranges[index].second.first);
        CAMERA_TEST_CHECK(ranges[index].second.second == expected_ranges[index].second.second);
    }

    // Out-of-range numbers are clamped, stored as integers, and foreign keys survive.
    write_text(clamp_path,
        "{\"version\":99,\"self_mask\":{\"enabled\":true},"
        "\"controls\":{\"unknown_control\":123,\"exposure\":99999,\"gain\":-5,"
        "\"brightness\":1000,\"gamma\":1,\"saturation\":200,\"contrast\":100,"
        "\"power_line_frequency\":9,\"exposure_dynamic_framerate\":4}}");
    {
        CamSettingsConfig settings;
        CAMERA_TEST_CHECK(settings.load(clamp_path.string()));
        CAMERA_TEST_CHECK(settings.get("exposure") == 5000.0F);
        CAMERA_TEST_CHECK(settings.get("gain") == 0.0F);
        CAMERA_TEST_CHECK(settings.get("brightness") == 64.0F);
        CAMERA_TEST_CHECK(settings.get("gamma") == 72.0F);
        CAMERA_TEST_CHECK(settings.get("saturation") == 128.0F);
        CAMERA_TEST_CHECK(settings.get("contrast") == 64.0F);
        CAMERA_TEST_CHECK(settings.get("power_line_frequency") == 2.0F);
        CAMERA_TEST_CHECK(settings.get("exposure_dynamic_framerate") == 1.0F);

        // Unknown names are rejected by set() and never returned by get().
        CAMERA_TEST_CHECK(!settings.set("unknown_control", 7));
        CAMERA_TEST_CHECK(!settings.get("unknown_control").has_value());
        CAMERA_TEST_CHECK(!settings.set("totally_new", 1));
        CAMERA_TEST_CHECK(!settings.get("totally_new").has_value());

        CAMERA_TEST_CHECK(settings.set("power_line_frequency", 7));
        CAMERA_TEST_CHECK(settings.get("power_line_frequency") == 2.0F);
        CAMERA_TEST_CHECK(settings.save());
    }
    {
        const nlohmann::json after = read_json(clamp_path);
        CAMERA_TEST_CHECK(after.at("self_mask").at("enabled") == true);
        CAMERA_TEST_CHECK(after.at("controls").at("unknown_control") == 123);
        CAMERA_TEST_CHECK(after.at("controls").at("exposure").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("exposure") == 5000);
        CAMERA_TEST_CHECK(after.at("controls").at("gain").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("gain") == 0);
        CAMERA_TEST_CHECK(after.at("controls").contains("totally_new") == false);
        CAMERA_TEST_CHECK(after.at("version") == 1);
        CAMERA_TEST_CHECK(is_iso_local_timestamp(after.at("ts")));
    }

    // Missing and non-numeric values fall back to safe defaults; numeric values clamp.
    write_text(defaults_path,
        "{\"controls\":{\"exposure\":\"bad\",\"gain\":null,\"brightness\":{},"
        "\"gamma\":true,\"saturation\":[1],\"contrast\":999}}");
    {
        CamSettingsConfig settings;
        CAMERA_TEST_CHECK(settings.load(defaults_path.string()));
        CAMERA_TEST_CHECK(settings.get("exposure") == 300.0F);
        CAMERA_TEST_CHECK(settings.get("gain") == 0.0F);
        CAMERA_TEST_CHECK(settings.get("brightness") == 0.0F);
        CAMERA_TEST_CHECK(settings.get("gamma") == 100.0F);
        CAMERA_TEST_CHECK(settings.get("saturation") == 60.0F);
        CAMERA_TEST_CHECK(settings.get("contrast") == 64.0F);
        CAMERA_TEST_CHECK(settings.get("power_line_frequency") == 1.0F);
        CAMERA_TEST_CHECK(settings.get("exposure_dynamic_framerate") == 0.0F);
        CAMERA_TEST_CHECK(settings.save());
    }
    {
        const nlohmann::json after = read_json(defaults_path);
        CAMERA_TEST_CHECK(after.at("controls").at("exposure").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("exposure") == 300);
        CAMERA_TEST_CHECK(after.at("controls").at("gamma") == 100);
        CAMERA_TEST_CHECK(after.at("controls").at("saturation") == 60);
        CAMERA_TEST_CHECK(after.at("controls").at("power_line_frequency") == 1);
        CAMERA_TEST_CHECK(after.at("controls").at("exposure_dynamic_framerate") == 0);
    }

    // A fresh load clears stale state, even when the new file is absent or partial.
    write_text(stale_a_path, "{\"controls\":{\"exposure\":1234,\"gain\":55}}");
    write_text(stale_b_path, "{\"controls\":{\"gain\":7}}");
    {
        CamSettingsConfig settings;
        CAMERA_TEST_CHECK(settings.load(stale_a_path.string()));
        CAMERA_TEST_CHECK(settings.get("exposure") == 1234.0F);
        CAMERA_TEST_CHECK(settings.get("gain") == 55.0F);
        CAMERA_TEST_CHECK(settings.load(stale_b_path.string()));
        CAMERA_TEST_CHECK(settings.get("exposure") == 300.0F);
        CAMERA_TEST_CHECK(settings.get("gain") == 7.0F);
        CAMERA_TEST_CHECK(settings.load(missing_path.string()));
        CAMERA_TEST_CHECK(settings.get("exposure") == 300.0F);
        CAMERA_TEST_CHECK(settings.get("gain") == 0.0F);
        CAMERA_TEST_CHECK(settings.save());
        const nlohmann::json after = read_json(missing_path);
        CAMERA_TEST_CHECK(after.at("controls").at("exposure") == 300);
        CAMERA_TEST_CHECK(after.at("controls").at("gain") == 0);
    }

    // Fractional values are rounded with lround and persisted as integers;
    // save() also writes ISO local ts and version=1.
    {
        CamSettingsConfig settings;
        CAMERA_TEST_CHECK(!settings.get("exposure").has_value());
        CAMERA_TEST_CHECK(settings.load(ts_path.string()));
        CAMERA_TEST_CHECK(settings.set("exposure", 299.4F));
        CAMERA_TEST_CHECK(settings.set("gain", 12.6F));
        CAMERA_TEST_CHECK(settings.set("brightness", -20.2F));
        CAMERA_TEST_CHECK(settings.set("gamma", 100.49F));
        CAMERA_TEST_CHECK(settings.set("saturation", 60.51F));
        CAMERA_TEST_CHECK(settings.set("contrast", 32.0F));
        CAMERA_TEST_CHECK(settings.set("power_line_frequency", 1.2F));
        CAMERA_TEST_CHECK(settings.set("exposure_dynamic_framerate", 0.6F));
        CAMERA_TEST_CHECK(!settings.get("unknown_control").has_value());
        CAMERA_TEST_CHECK(settings.save());
    }
    {
        const nlohmann::json after = read_json(ts_path);
        CAMERA_TEST_CHECK(after.is_object());
        CAMERA_TEST_CHECK(after.at("version") == 1);
        CAMERA_TEST_CHECK(is_iso_local_timestamp(after.at("ts")));
        CAMERA_TEST_CHECK(after.at("controls").is_object());
        CAMERA_TEST_CHECK(after.at("controls").at("exposure").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("exposure") == 299);
        CAMERA_TEST_CHECK(after.at("controls").at("gain").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("gain") == 13);
        CAMERA_TEST_CHECK(after.at("controls").at("brightness").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("brightness") == -20);
        CAMERA_TEST_CHECK(after.at("controls").at("gamma").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("gamma") == 100);
        CAMERA_TEST_CHECK(after.at("controls").at("saturation").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("saturation") == 61);
        CAMERA_TEST_CHECK(after.at("controls").at("contrast").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("contrast") == 32);
        CAMERA_TEST_CHECK(after.at("controls").at("power_line_frequency").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("power_line_frequency") == 1);
        CAMERA_TEST_CHECK(after.at("controls").at("exposure_dynamic_framerate").is_number_integer());
        CAMERA_TEST_CHECK(after.at("controls").at("exposure_dynamic_framerate") == 1);
        CAMERA_TEST_CHECK(after.at("controls").contains("unknown_control") == false);
    }

    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
    return 0;
}
