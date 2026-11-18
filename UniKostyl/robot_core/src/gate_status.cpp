#include "gate_status.hpp"
#include <nlohmann/json.hpp>

namespace robot_core {

using nlohmann::json;

float camera_angle_to_robot(float camera_deg) {
    // rcj gate.cpp Gate::countAngle(): angle -= 90; if (angle > 180) -= 360.
    // Wrap to 0..360 first (lenient; identical on the reference domain).
    float a = wrap360(camera_deg) - 90.0f;
    if (a > 180.0f) {
        a -= 360.0f;
    }
    return a;
}

namespace {

// Lenient getters: wrong type or missing key -> fallback, no throw.
bool get_bool(const json& j, const char* key, bool& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return false;
    out = it->get<bool>();
    return true;
}

bool get_number(const json& j, const char* key, double& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return false;
    out = it->get<double>();
    return true;
}

void parse_gate_entry(const json& entry, GateInput& g) {
    g.configured = true;
    bool enabled = false;
    if (get_bool(entry, "enabled", enabled) && !enabled) {
        // A disabled slot tracks nothing (PROMPT_EN §4): treat as not found
        // but still configured (so the UI/log can distinguish the reason).
        g.found = false;
    }
    bool found = false;
    if (get_bool(entry, "found", found)) {
        g.found = found;
    }
    if (!g.found) {
        // found:false -> no angle/area keys per the contract; hold nothing.
        g.has_angle = false;
        return;
    }
    double v = 0.0;
    if (get_number(entry, "angle", v)) {
        g.angle = Heading(camera_angle_to_robot((float)v));
        g.has_angle = true;
    }
    if (get_number(entry, "area", v)) {
        g.area = (int)v;
    }
    if (get_number(entry, "dist_cm", v)) {
        g.has_dist = true;
        g.dist_cm = (float)v;
    }
    bool rel = false;
    if (get_bool(entry, "dist_reliable", rel)) {
        g.dist_reliable = rel;
    }
    auto it = entry.find("role");
    if (it != entry.end() && it->is_string()) {
        std::string role = it->get<std::string>();
        if (role == "ours") g.role = GateRole::kOurs;
        else if (role == "opponent") g.role = GateRole::kOpponent;
        else g.role = GateRole::kUnset;  // unset == absence of the key (Idea 9)
    }
}

}  // namespace

bool parse_gate_status(const std::string& payload, unsigned long now_ms,
                       GateStatusView& out) {
    out = GateStatusView{};
    json root;
    try {
        root = json::parse(payload);
    } catch (...) {
        // Malformed JSON: drop the payload entirely, keep the previous view
        // (the caller holds the last fresh view; staleness handles the rest).
        return false;
    }
    if (!root.is_object()) {
        return false;
    }
    auto gates = root.find("gates");
    if (gates == root.end() || !gates->is_object()) {
        return false;
    }
    double fps = 0.0;
    if (get_number(root, "fps", fps)) {
        out.fps = (float)fps;
    }
    for (int id = 1; id <= 3; id++) {
        char key[8];
        std::snprintf(key, sizeof(key), "%d", id);
        auto it = gates->find(key);
        if (it == gates->end() || !it->is_object()) {
            continue;  // gate absent from this status: stays unconfigured
        }
        parse_gate_entry(*it, out.gates[id]);
    }
    out.valid = true;
    out.received_ms = now_ms;
    return true;
}

bool gate_status_fresh(const GateStatusView& view, unsigned long now_ms,
                       unsigned long stale_ms) {
    if (!view.valid) {
        return false;
    }
    return now_ms - view.received_ms <= stale_ms;
}

GateRole gate_role(const GateStatusView& view, int id) {
    if (id < 1 || id > 3) {
        return GateRole::kUnset;
    }
    return view.gates[id].role;
}

}  // namespace robot_core
