#include "cam_settings_config.hpp"

#include <algorithm>
#include <cmath>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {

namespace {

struct Range {
    const char* name;
    int min;
    int max;
};

constexpr Range kRanges[] = {
    {"exposure", 1, 5000},
    {"gain", 0, 100},
    {"brightness", -64, 64},
    {"gamma", 72, 500},
    {"saturation", 0, 128},
    {"contrast", 0, 64},
    {"power_line_frequency", 0, 2},
    {"exposure_dynamic_framerate", 0, 1},
};

float clamp_control(const std::string& control, float value) {
    for (const auto& range : kRanges) {
        if (control == range.name) {
            return std::clamp(value, static_cast<float>(range.min), static_cast<float>(range.max));
        }
    }
    return value;
}

} // namespace

bool CamSettingsConfig::load(const std::string& path) {
    path_ = path;
    std::string warning;
    if (auto loaded = read_json_file(path_, &warning); loaded && loaded->is_object()) {
        root_ = *loaded;
    }
    if (!root_.contains("version")) root_["version"] = 1;
    if (!root_.contains("controls") || !root_["controls"].is_object()) {
        root_["controls"] = nlohmann::json::object();
    }
    for (const auto& range : kRanges) {
        nlohmann::json& value = root_["controls"][range.name];
        if (value.is_number()) {
            value = clamp_control(range.name, value.get<float>());
        }
    }
    return true;
}

std::optional<float> CamSettingsConfig::get(const std::string& control) const {
    if (!root_.contains("controls") || !root_["controls"].is_object()) return std::nullopt;
    const auto& value = root_["controls"][control];
    if (!value.is_number()) return std::nullopt;
    return clamp_control(control, value.get<float>());
}

bool CamSettingsConfig::set(const std::string& control, float value) {
    if (!root_.is_object()) root_ = nlohmann::json::object();
    if (!root_.contains("controls") || !root_["controls"].is_object()) {
        root_["controls"] = nlohmann::json::object();
    }
    root_["controls"][control] = clamp_control(control, value);
    return true;
}

bool CamSettingsConfig::save() const {
    std::string error;
    return atomic_write_text(path_, root_.dump(2), &error);
}

std::vector<std::pair<std::string, std::pair<int, int>>> CamSettingsConfig::ranges() {
    std::vector<std::pair<std::string, std::pair<int, int>>> out;
    out.reserve(std::size(kRanges));
    for (const auto& range : kRanges) {
        out.emplace_back(range.name, std::pair<int, int>{range.min, range.max});
    }
    return out;
}

} // namespace camera_cpp
