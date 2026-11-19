#include "field_map.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <optional>
#include <string>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {

namespace {

constexpr float kDistMaxCm = 500.0F;

nlohmann::json default_root() {
    return nlohmann::json{
        {"version", 1},
        {"bound_at", nullptr},
        {"robot_pos", nullptr},
        {"heading_offset_deg", 0.0},
        {"gates", nlohmann::json::object()},
    };
}

bool finite_number(const nlohmann::json& value, float* out) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    if (!std::isfinite(number)) return false;
    *out = static_cast<float>(number);
    return true;
}

float wrap_angle(float angle) {
    float wrapped = std::fmod(angle, 360.0F);
    if (wrapped < 0.0F) wrapped += 360.0F;
    return wrapped;
}

std::optional<float> clamp_angle(float angle) {
    if (!std::isfinite(angle)) return std::nullopt;
    return wrap_angle(angle);
}

std::optional<float> clamp_dist(float dist) {
    if (!std::isfinite(dist) || dist <= 0.0F) return std::nullopt;
    return std::min(dist, kDistMaxCm);
}

std::optional<float> clamp_angle(const nlohmann::json& value) {
    float angle = 0.0F;
    if (!finite_number(value, &angle)) return std::nullopt;
    return wrap_angle(angle);
}

std::optional<float> clamp_dist(const nlohmann::json& value) {
    float dist = 0.0F;
    if (!finite_number(value, &dist)) return std::nullopt;
    return clamp_dist(dist);
}

bool valid_gate_key(const std::string& key) {
    return key == "1" || key == "2" || key == "3";
}

bool json_truthy(const nlohmann::json& value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>() != 0.0;
    if (value.is_string()) return !value.get_ref<const std::string&>().empty();
    if (value.is_array() || value.is_object()) return !value.empty();
    return false;
}

std::optional<nlohmann::json> normalize_gate_entry(const nlohmann::json& raw) {
    if (!raw.is_object()) return std::nullopt;
    const auto angle = clamp_angle(raw.value("angle_deg", nlohmann::json()));
    if (!angle) return std::nullopt;
    const auto dist = clamp_dist(raw.value("dist_cm", nlohmann::json()));
    return nlohmann::json{
        {"angle_deg", *angle},
        {"dist_cm", dist ? nlohmann::json(*dist) : nlohmann::json(nullptr)},
        {"estimated", raw.contains("estimated") && json_truthy(raw["estimated"])},
    };
}

int parse_version(const nlohmann::json& value) {
    if (value.is_number()) {
        const double number = value.get<double>();
        return std::isfinite(number) && number == 1.0 ? 1 : 0;
    }
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>()) == 1 ? 1 : 0;
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string now_ts() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return std::string(buffer);
}

} // namespace

FieldMap::FieldMap(float, float, float) : root_(default_root()) {}

// Загружает привязку ворот к полю и сохраняет неизвестные ключи в original_.
// Некорректные angles/distances нормализуются или отбрасываются.
bool FieldMap::load(const std::string& path) {
    path_ = path;
    std::string warning;
    const auto loaded = read_json_file(path_, &warning);
    if (!loaded || !loaded->is_object() || !loaded->contains("version") ||
        parse_version((*loaded)["version"]) != 1) {
        root_ = default_root();
        return false;
    }

    root_ = *loaded;

    const auto& raw_pos = root_.contains("robot_pos") ? root_["robot_pos"] : nlohmann::json();
    if (raw_pos.is_string()) {
        const auto& text = raw_pos.get_ref<const std::string&>();
        root_["robot_pos"] = (text == "center" || text == "own_goal" || text == "manual")
                                 ? nlohmann::json(text)
                                 : nlohmann::json(nullptr);
    } else {
        root_["robot_pos"] = nullptr;
    }

    float heading = 0.0F;
    if (root_.contains("heading_offset_deg")) {
        float parsed = 0.0F;
        if (finite_number(root_["heading_offset_deg"], &parsed)) heading = parsed;
    }
    root_["heading_offset_deg"] = static_cast<double>(heading);

    if (!root_.contains("bound_at") ||
        !(root_["bound_at"].is_string() || root_["bound_at"].is_null())) {
        root_["bound_at"] = nullptr;
    }

    nlohmann::json gates = nlohmann::json::object();
    const auto raw_gates = root_.value("gates", nlohmann::json::object());
    if (raw_gates.is_object()) {
        for (auto it = raw_gates.begin(); it != raw_gates.end(); ++it) {
            if (!valid_gate_key(it.key())) continue;
            auto entry = normalize_gate_entry(it.value());
            if (entry) gates[it.key()] = *entry;
        }
    }
    root_["gates"] = std::move(gates);
    return true;
}

FieldBindResult FieldMap::bind_from_results(const std::vector<FieldObservationInput>& observations,
                                            float heading_offset_deg,
                                            float) {
    nlohmann::json gates = nlohmann::json::object();
    for (const auto& observation : observations) {
        if (!observation.enabled || !observation.found) continue;
        const int gate_id = observation.slot + 1;
        if (!valid_gate_key(std::to_string(gate_id))) continue;
        const auto angle = clamp_angle(observation.angle);
        if (!angle) continue;
        const auto dist = clamp_dist(observation.dist_cm);
        gates[std::to_string(gate_id)] = nlohmann::json{
            {"angle_deg", *angle},
            {"dist_cm", dist ? nlohmann::json(*dist) : nlohmann::json(nullptr)},
            {"estimated", observation.held},
        };
    }
    if (gates.empty()) return FieldBindResult{false, "no measured gates"};

    root_["heading_offset_deg"] = static_cast<double>(
        std::isfinite(heading_offset_deg) ? heading_offset_deg : 0.0F);
    root_["gates"] = std::move(gates);
    root_["bound_at"] = now_ts();
    return FieldBindResult{true, ""};
}

// Атомарно сохраняет field_map.json, сохраняя неизвестные ключи исходного файла.
bool FieldMap::save_atomic() const {
    std::string error;
    return atomic_write_text(path_, root_.dump(2), &error);
}

std::map<int, EstimatedDirection> FieldMap::estimated_directions(float heading_delta_deg) const {
    std::map<int, EstimatedDirection> out;
    if (!root_.contains("gates") || !root_["gates"].is_object()) return out;
    const float delta = std::isfinite(heading_delta_deg) ? heading_delta_deg : 0.0F;
    for (auto it = root_["gates"].begin(); it != root_["gates"].end(); ++it) {
        if (!valid_gate_key(it.key()) || !it.value().is_object()) continue;
        const auto angle = clamp_angle(it.value().value("angle_deg", nlohmann::json()));
        if (!angle) continue;
        const auto dist = clamp_dist(it.value().value("dist_cm", nlohmann::json()));
        out[it.key()[0] - '0'] = EstimatedDirection{wrap_angle(*angle - delta),
                                                    dist ? *dist : 0.0F, true};
    }
    return out;
}

} // namespace camera_cpp
