// pi_core/color_message.hpp — parsing of the laptop's color messages on ZMQ
// 5557 (pure logic, nlohmann/json; malformed input never throws).
//
// Accepted (PROMPT_EN.md section 4 contract):
//   {"gates":[{"id":N,"rgb":[r,g,b]},...]}   gates 1..3
//   {"rgb":[r,g,b]}                          legacy = gate 1
//   {"stream":true|false}                    video on/off
// Idea 9 (additive): each gate item may carry "role":"ours"|"opponent";
// it is optional ("only when set" — the laptop never sends "unset") and
// unknown/invalid role values are ignored, exactly like unknown fields.
// Any combination may appear in one message. Unknown fields are ignored.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace pi_core {

struct ColorsMessage {
    bool valid = false;  // top-level JSON object (Python: isinstance(dict))
    bool has_stream = false;
    bool stream_value = false;
    std::vector<std::pair<int, std::array<int, 3>>> gates;  // id 1-based
    // Idea 9: gate_roles[i] is the role of gates[i] — "ours"/"opponent",
    // or empty when the item had no (valid) role. Kept 1:1 with gates by
    // the parser: one push per accepted item.
    std::vector<std::string> gate_roles;
    bool has_legacy_rgb = false;
    std::array<int, 3> legacy_rgb = {0, 0, 0};
};

// Never throws; on any structural problem the corresponding part is skipped,
// exactly like the Python try/except in RGBListener.run().
ColorsMessage parse_colors_message(const std::string& payload);

}  // namespace pi_core
