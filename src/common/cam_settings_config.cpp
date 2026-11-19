#include "cam_settings_config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {

namespace {

struct ControlSpec {
    const char* name;
    int min;
    int max;
    int default_value;
};

// Single canonical table for ranges and safe defaults (legacy CONTROLS).
constexpr ControlSpec kControls[] = {
    {"exposure", 1, 5000, 300},
    {"gain", 0, 100, 0},
    {"brightness", -64, 64, 0},
    {"gamma", 72, 500, 100},
    {"saturation", 0, 128, 60},
    {"contrast", 0, 64, 32},
    {"power_line_frequency", 0, 2, 1},
    {"exposure_dynamic_framerate", 0, 1, 0},
};

const ControlSpec* find_control(const std::string& name) {
    for (const auto& control : kControls) {
        if (name == control.name) return &control;
    }
    return nullptr;
}

int round_and_clamp(const ControlSpec& control, double value) {
    if (!std::isfinite(value)) value = static_cast<double>(control.default_value);
    const double bounded = std::clamp(value, static_cast<double>(control.min),
                                      static_cast<double>(control.max));
    return static_cast<int>(std::lround(bounded));
}

int normalized_value(const ControlSpec& control, const nlohmann::json* value) {
    if (!value || !value->is_number()) return control.default_value;
    return round_and_clamp(control, value->get<double>());
}

// Fills missing/non-numeric controls with defaults and replaces every known
// control with a clamped integer. Unknown root and control keys are untouched.
void normalize_controls(nlohmann::json& root) {
    if (!root.is_object()) root = nlohmann::json::object();
    if (!root.contains("controls") || !root["controls"].is_object()) {
        root["controls"] = nlohmann::json::object();
    }
    nlohmann::json& controls = root["controls"];
    for (const auto& control : kControls) {
        const auto found = controls.find(control.name);
        const nlohmann::json* value = found == controls.end() ? nullptr : &*found;
        controls[control.name] = normalized_value(control, value);
    }
}

std::string local_iso_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream out;
    out << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

} // namespace

// Загружает настройки камеры, нормализуя управления по разрешённым диапазонам.
// auto_exposure принудительно остаётся включённым: ручной режим не поддерживается.
bool CamSettingsConfig::load(const std::string& path) {
    path_ = path;
    root_ = nlohmann::json::object();
    std::string warning;
    if (auto loaded = read_json_file(path_, &warning); loaded && loaded->is_object()) {
        root_ = *loaded;
    }
    if (!root_.contains("version")) root_["version"] = 1;
    normalize_controls(root_);
    return true;
}

std::optional<float> CamSettingsConfig::get(const std::string& control) const {
    const ControlSpec* spec = find_control(control);
    if (!spec) return std::nullopt;
    const auto controls = root_.find("controls");
    if (controls == root_.end() || !controls->is_object()) return std::nullopt;
    const auto value = controls->find(control);
    if (value == controls->end() || !value->is_number()) return std::nullopt;
    return static_cast<float>(normalized_value(*spec, &*value));
}

bool CamSettingsConfig::set(const std::string& control, float value) {
    const ControlSpec* spec = find_control(control);
    if (!spec) return false;
    if (!root_.is_object()) root_ = nlohmann::json::object();
    if (!root_.contains("controls") || !root_["controls"].is_object()) {
        root_["controls"] = nlohmann::json::object();
    }
    root_["controls"][control] = round_and_clamp(*spec, static_cast<double>(value));
    return true;
}

// Атомарно сохраняет настройки камеры, сохраняя неизвестные ключи конфига.
bool CamSettingsConfig::save() const {
    nlohmann::json output = root_;
    normalize_controls(output);
    output["version"] = 1;
    output["ts"] = local_iso_timestamp();
    std::string error;
    return atomic_write_text(path_, output.dump(2), &error);
}

std::vector<std::pair<std::string, std::pair<int, int>>> CamSettingsConfig::ranges() {
    std::vector<std::pair<std::string, std::pair<int, int>>> out;
    for (const auto& control : kControls) {
        out.emplace_back(control.name, std::pair<int, int>{control.min, control.max});
    }
    return out;
}

} // namespace camera_cpp
