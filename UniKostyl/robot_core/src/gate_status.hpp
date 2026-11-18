#pragma once
// gate_status.hpp — camera gate status consumption (the contract robot_core
// CONSUMES from the camera project, PROMPT_EN §4 + Idea 2 / COORDINATION.md).
//
// Wire contract (multipart ZMQ [JPEG, JSON status] on tcp://*:5555, part 2):
//   {"gates":{"1":{"enabled":true,"found":true,"angle":123,"area":456,
//              "dist_cm":148.0,"dist_reliable":true,"role":"ours"},...},
//    "fps":25.9}
// Optional per-gate fields (dist_cm/dist_reliable from Idea 6, role from
// Idea 9) may be absent; keys are strings "1".."3"; found:false => no
// angle/area keys. Parsed LENIENTLY: wrong types / missing keys / garbage =
// that field (or the whole status) reads as absent — never throw, never
// block, never crash (PROMPT_ROBOT §4: absent camera = graceful ball-only /
// line-only degradation, log the reason).
//
// Angle convention: camera angles are 0..360 deg (0° = right, CCW). The
// reference firmware's gate.cpp countAngle() applied the robot-frame
// conversion `angle -= 90; if (angle > 180) angle -= 360;` — ported here as
// camera_angle_to_robot() (wrap first, then the same conversion).

#include "angles.hpp"
#include <string>

namespace robot_core {

enum class GateRole { kUnset, kOurs, kOpponent };

struct GateInput {
    bool configured = false;    // slot present in the status JSON at all
    bool found = false;
    Heading angle;              // robot frame, -180..180 (camera_angle_to_robot)
    bool has_angle = false;
    int area = 0;
    bool has_dist = false;
    float dist_cm = 0.0f;
    bool dist_reliable = false;
    GateRole role = GateRole::kUnset;
};

struct GateStatusView {
    bool valid = false;         // a parseable status was seen (freshness below)
    unsigned long received_ms = 0;
    float fps = 0.0f;
    GateInput gates[4];         // index 1..3 = camera gate ids (0 unused)
};

// rcj gate.cpp countAngle(): angle -= 90; if (angle > 180) angle -= 360.
// Input is wrapped to 0..360 first (lenient extension; identical on the
// reference's valid domain).
float camera_angle_to_robot(float camera_deg);

// Parse a status JSON payload (lenient). `now_ms` stamps freshness.
// Returns false when the payload cannot be used at all (no "gates" object).
bool parse_gate_status(const std::string& json, unsigned long now_ms,
                       GateStatusView& out);

// True when the view is fresh: received within `stale_ms` (default 1000).
bool gate_status_fresh(const GateStatusView& view, unsigned long now_ms,
                       unsigned long stale_ms = 1000);

// Role lookup helper (gate id 1..3).
GateRole gate_role(const GateStatusView& view, int id);

}  // namespace robot_core
