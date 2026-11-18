#include "field_map.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {

namespace {

float normalize_angle(float angle) {
    while (angle > 180.0F) angle -= 360.0F;
    while (angle <= -180.0F) angle += 360.0F;
    return angle;
}

} // namespace

FieldMap::FieldMap(float field_width_cm, float gate_width_cm, float gate_height_cm)
    : field_width_cm_(field_width_cm), gate_width_cm_(gate_width_cm), gate_height_cm_(gate_height_cm) {
    root_ = nlohmann::json{
        {"version", 1},
        {"field_width_cm", field_width_cm_},
        {"gate_width_cm", gate_width_cm_},
        {"gate_height_cm", gate_height_cm_},
        {"gates", nlohmann::json::object()},
    };
}

bool FieldMap::load(const std::string& path) {
    path_ = path;
    std::string warning;
    if (auto loaded = read_json_file(path_, &warning); loaded && loaded->is_object()) {
        root_ = *loaded;
    }
    if (!root_.contains("version")) root_["version"] = 1;
    if (!root_.contains("gates") || !root_["gates"].is_object()) {
        root_["gates"] = nlohmann::json::object();
    }
    return true;
}

FieldBindResult FieldMap::bind_from_results(const std::vector<FieldObservationInput>& observations,
                                            float heading_offset_deg,
                                            float heading_delta_deg) {
    std::vector<std::pair<int, const FieldObservationInput*>> measured;
    for (const auto& observation : observations) {
        if (observation.enabled && observation.found) {
            int gate_id = observation.gate_id >= 1 ? observation.gate_id : 1;
            measured.emplace_back(gate_id, &observation);
        }
    }

    if (measured.empty()) {
        return FieldBindResult{false, "no measured gates"};
    }

    root_["heading_offset_deg"] = heading_offset_deg;
    root_["heading_delta_deg"] = heading_delta_deg;
    for (const auto& [gate_id, observation] : measured) {
        nlohmann::json gate = root_["gates"].value(std::to_string(gate_id), nlohmann::json::object());
        gate["angle_deg"] = observation->angle_deg;
        gate["dist_cm"] = observation->reliable ? nlohmann::json(observation->dist_cm) : nlohmann::json(nullptr);
        gate["estimated"] = true;
        gate["enabled"] = true;
        gate["found"] = true;
        root_["gates"][std::to_string(gate_id)] = gate;
    }
    return FieldBindResult{true, ""};
}

bool FieldMap::save_atomic() const {
    std::string error;
    return atomic_write_text(path_, root_.dump(2), &error);
}

std::map<int, EstimatedDirection> FieldMap::estimated_directions(float heading_delta_deg) const {
    std::map<int, EstimatedDirection> out;
    if (!root_.contains("gates") || !root_["gates"].is_object()) return out;
    for (const auto& [key, gate] : root_["gates"].items()) {
        if (!gate.is_object() || !gate.contains("angle_deg")) continue;
        try {
            const int id = std::stoi(key);
            const float angle = gate["angle_deg"].get<float>();
            out[id] = EstimatedDirection{normalize_angle(angle + heading_delta_deg), true};
        } catch (...) {
            continue;
        }
    }
    return out;
}

} // namespace camera_cpp
