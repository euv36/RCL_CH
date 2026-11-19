#include "laptop/status_panel_client.hpp"

#include <cmath>

#include "nlohmann/json.hpp"

namespace camera_cpp::laptop {
namespace {

using json = nlohmann::json;

// Читает булево поле, подменяя отсутствие или неверный тип значением по умолчанию.
bool read_bool(const json& object, const char* key, bool fallback = false) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

// Читает числовое поле, отбрасывая нечисловые и нефинальные значения.
float read_float(const json& object, const char* key, float fallback = 0.0F) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) return fallback;
    const double value = it->get<double>();
    return std::isfinite(value) ? static_cast<float>(value) : fallback;
}

// Читает строковое поле, подменяя отсутствие или неверный тип значением по умолчанию.
std::string read_string(const json& object, const char* key, const std::string& fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

// Заполняет ворота из JSON; отключённые ворота никогда не сохраняют found/held.
void read_gate(const json& object, GateStatusView& gate) {
    gate.id = static_cast<int>(read_float(object, "id", static_cast<float>(gate.id)));
    gate.enabled = read_bool(object, "enabled");
    gate.role = read_string(object, "role", "unset");
    gate.angle_deg = read_float(object, "angle");
    gate.area_px = read_float(object, "area");
    if (gate.area_px <= 0.0F) gate.area_px = read_float(object, "area_px");
    gate.dist_cm = read_float(object, "dist_cm");
    gate.dist_reliable = read_bool(object, "dist_reliable");
    gate.found = read_bool(object, "found") && gate.enabled;
    gate.held = read_bool(object, "held") && gate.enabled;
    if (!gate.enabled) {
        gate.angle_deg = 0.0F;
        gate.area_px = 0.0F;
        gate.dist_cm = 0.0F;
        gate.dist_reliable = false;
    }
}

} // namespace

// Разбирает статус Pi без исключений: неверный JSON просто помечается невалидным.
ParsedStatus parse_status_payload(std::string_view text,
                                  std::chrono::steady_clock::time_point received_at,
                                  std::chrono::steady_clock::time_point now,
                                  double stale_seconds) {
    ParsedStatus result;
    const json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) return result;

    const auto gates_it = document.find("gates");
    if (gates_it != document.end() && gates_it->is_object()) {
        for (std::size_t index = 0; index < result.gates.size(); ++index) {
            auto& gate = result.gates[index];
            gate.id = static_cast<int>(index) + 1;
            const auto it = gates_it->find(std::to_string(gate.id));
            if (it != gates_it->end() && it->is_object()) read_gate(*it, gate);
        }
    }
    result.fps = read_float(document, "fps");
    result.src = read_string(document, "src", "");
    const auto frame_wh = document.find("frame_wh");
    if (frame_wh != document.end() && frame_wh->is_array() && frame_wh->size() >= 2) {
        if ((*frame_wh)[0].is_number()) result.frame_wh[0] = static_cast<int>((*frame_wh)[0].get<double>());
        if ((*frame_wh)[1].is_number()) result.frame_wh[1] = static_cast<int>((*frame_wh)[1].get<double>());
    }
    result.valid = true;
    result.stale = std::chrono::duration<double>(now - received_at).count() > stale_seconds;
    return result;
}

} // namespace camera_cpp::laptop
