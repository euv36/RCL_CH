// pi_core/color_message.cpp — see color_message.hpp
#include "color_message.hpp"

#include <nlohmann/json.hpp>

namespace pi_core {

namespace {
using json = nlohmann::json;

// Python bool(data["stream"]) truthiness for arbitrary JSON values.
bool python_truthy(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) {
        if (v.is_number_integer()) return v.get<long long>() != 0;
        return v.get<double>() != 0.0;
    }
    if (v.is_string()) return !v.get<std::string>().empty();
    if (v.is_array() || v.is_object()) return !v.empty();
    return false;  // null
}

// int(item["rgb"][i]) equivalent: accepts integer or float JSON numbers,
// truncates like Python int(). Non-numbers fail -> item skipped.
bool to_int(const json& v, int& out) {
    if (!v.is_number()) return false;
    if (v.is_number_integer()) {
        out = static_cast<int>(v.get<long long>());
    } else {
        out = static_cast<int>(v.get<double>());  // Python int() truncates
    }
    return true;
}

// Clamps an RGB component into 0..255 (colors out of range would otherwise
// produce a degenerate HSV range; Python would wrap into cv2 silently).
int clamp255(int x) { return x < 0 ? 0 : (x > 255 ? 255 : x); }

bool parse_rgb3(const json& arr, std::array<int, 3>& out) {
    if (!arr.is_array() || arr.size() < 3) return false;
    for (int i = 0; i < 3; ++i) {
        int x;
        if (!to_int(arr[static_cast<size_t>(i)], x)) return false;
        out[i] = clamp255(x);
    }
    return true;
}
}  // namespace

ColorsMessage parse_colors_message(const std::string& payload) {
    ColorsMessage msg;

    json doc = json::parse(payload, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return msg;  // valid=false

    msg.valid = true;

    auto stream_it = doc.find("stream");
    if (stream_it != doc.end()) {
        msg.has_stream = true;
        msg.stream_value = python_truthy(*stream_it);
    }

    auto gates_it = doc.find("gates");
    if (gates_it != doc.end() && gates_it->is_array()) {
        for (const auto& item : *gates_it) {
            if (!item.is_object()) continue;
            auto rgb_it = item.find("rgb");
            if (rgb_it == item.end()) continue;  // Python: rgb is None -> skip
            std::array<int, 3> rgb{0, 0, 0};
            if (!parse_rgb3(*rgb_it, rgb)) continue;  // bad rgb -> skip item
            // Python: int(item.get("id", 1)) — missing id means gate 1.
            int id = 1;
            auto id_it = item.find("id");
            if (id_it != item.end()) {
                if (!to_int(*id_it, id)) continue;
            }
            msg.gates.emplace_back(id, rgb);
            // Idea 9: optional "role" per gate — kept 1:1 with gates
            // (one push per accepted item). Only "ours"/"opponent" are
            // accepted; absent/"unset"/unknown -> empty (= not set).
            std::string role;
            auto role_it = item.find("role");
            if (role_it != item.end() && role_it->is_string()) {
                const std::string& r = role_it->get<std::string>();
                if (r == "ours" || r == "opponent") role = r;
            }
            msg.gate_roles.push_back(std::move(role));
        }
    }
    // Legacy "rgb" is applied only when the "gates" key is absent entirely
    // (Python: `elif "rgb" in data`). If "gates" exists but is not an array,
    // Python's outer try/except skips the whole block — same here.

    if (gates_it == doc.end()) {
        auto rgb_it = doc.find("rgb");
        if (rgb_it != doc.end() && rgb_it->is_array()) {
            std::array<int, 3> rgb{0, 0, 0};
            if (parse_rgb3(*rgb_it, rgb)) {
                msg.has_legacy_rgb = true;
                msg.legacy_rgb = rgb;
            }
        }
    }

    return msg;
}

}  // namespace pi_core
