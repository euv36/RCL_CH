// pi_core/gate_logic.hpp — pure gate-tracking logic, no OpenCV / ZMQ / serial.
// Mirrors rpi_elp_cam.py (GateTracker.update_gate_from_rgb, process(),
// handle_serial()) so Python and C++ stay in parity and everything here can be
// unit-tested on any machine.
//
// Documented deliberate deviation (per PROMPT_EN.md section 2, "No bugs"):
// the angle-change threshold uses wrap-aware difference, so 359 -> 1 deg is
// 2 deg, not 358. Python rpi_elp_cam.py uses naive |a-b| and over-sends near
// 0 deg; the C++ core does not.
#pragma once

#include <string>
#include <vector>

namespace pi_core {

// Tolerance (H, S, V) applied to a received gate color, same as
// DEFAULT_TOLERANCE in rpi_elp_cam.py.
struct Tolerance {
    int h = 40;
    int s = 100;
    int v = 100;
};

// One inclusive hue interval on the OpenCV 0..179 scale.
struct HsvRange {
    int lo;  // inclusive
    int hi;  // inclusive
};

// Idea 9: gate role ("ours" = our goal attacks into it, "opponent" = the
// enemy goal). Wire/file carry "ours"/"opponent" only when set; absence
// (and any unknown value) means Unset. Plain enum — safe to copy in the
// per-frame params snapshot, no allocation.
enum class GateRole { Unset, Ours, Opponent };

// "ours"/"opponent" -> enum; everything else (incl. "unset"/absent) -> Unset.
inline GateRole gate_role_from_string(const std::string& role) {
    if (role == "ours") return GateRole::Ours;
    if (role == "opponent") return GateRole::Opponent;
    return GateRole::Unset;
}

// Per-gate inRange parameters, built from a received RGB pixel.
struct GateParams {
    bool enabled = false;
    std::vector<HsvRange> h_ranges;  // 1 or 2 ranges (wraparound)
    int s_lo = 0, s_hi = 255;
    int v_lo = 0, v_hi = 255;
    GateRole role = GateRole::Unset;  // Idea 9: metadata, hot path ignores it
};

// Builds the inRange parameters from an HSV pixel + tolerance.
// Returns false for degenerate input (h/s/v out of 0..179/0..255/0..255);
// ranges that end up empty after clamping are dropped, exactly like the
// Python list comprehension `[(max(0,a), min(179,b)) for a,b in ranges if b >= a]`.
bool build_gate_params(int h, int s, int v, const Tolerance& tol,
                       GateParams& out);

// Gate angle exactly like Python:
//   int(math.degrees(math.atan2(cy0 - cy, cx - cx0)) % 360)
// 0 deg = right, counter-clockwise positive; result 0..359.
int gate_angle(int cx, int cy, int cx0, int cy0);

// Wrap-aware |a-b| for angles in degrees, in [0, 180].
// 359 vs 1 -> 2 (Python naive diff would say 358).
int angle_diff_wrapped(int a, int b);

// ---------------- serial decision policy (pure) ----------------

constexpr double kHeartbeatInterval = 0.5;      // s, HEARTBEAT_INTERVAL
constexpr int kAngleChangeThreshold = 3;        // deg, ANGLE_CHANGE_THRESHOLD
constexpr double kMinSendInterval = 0.10;       // s, MIN_SEND_INTERVAL

struct SerialGateState {
    bool last_sent_found = false;     // "G<id>,<angle>,<area>" sent and alive
    bool has_last_sent_angle = false;
    int last_sent_angle = 0;
    double last_send_time = 0.0;      // monotonic seconds
};

enum class SerialAction { None, SendAngle, SendLost };

struct SerialDecision {
    SerialAction action = SerialAction::None;
    int angle = 0;
    int area = 0;  // truncated int(area), same as Python int(info['area'])
};

// Pure decision for one gate, one frame. `now` is monotonic seconds.
// - found:  send when angle changed by >= kAngleChangeThreshold (wrap-aware)
//           and >= kMinSendInterval since last send, or when nothing sent yet.
// - !found: send "G<id>,-1,0" exactly once (only if a detection was sent and
//           not yet cleared).
// The caller commits the result via commit_* only if the write succeeded;
// on a failed write the same decision is retried on the next frame.
SerialDecision decide_serial(const SerialGateState& st, bool found, int angle,
                             double area, double now);

void commit_sent_angle(SerialGateState& st, int angle, double now);
void commit_sent_lost(SerialGateState& st);

// True when a heartbeat "HB" is due (Python SerialLink.heartbeat: timer
// advances only if the write succeeds).
bool heartbeat_due(double last_heartbeat, double now);

// Exact serial line formats (contract, must not change):
//   G<id>,<angle>,<area>\n   and   G<id>,-1,0\n   and   HB\n
std::string format_gate_line(int gate_id, int angle, int area);
std::string format_lost_line(int gate_id);
constexpr const char* kHeartbeatLine = "HB\n";

// Future extension point (Idea 5 hysteresis): the lost trigger becomes
// "N consecutive empty frames" and a new detection is accepted after M
// consecutive frames. decide_serial() stays the formatting/commit point;
// wrap the found/lost inputs in a debounce filter before calling it.

}  // namespace pi_core
