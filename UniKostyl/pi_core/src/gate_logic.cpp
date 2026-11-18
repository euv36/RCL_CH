// pi_core/gate_logic.cpp — see gate_logic.hpp
#include "gate_logic.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

namespace pi_core {

bool build_gate_params(int h, int s, int v, const Tolerance& tol,
                       GateParams& out) {
    if (h < 0 || h > 179 || s < 0 || s > 255 || v < 0 || v > 255) {
        return false;
    }

    // Same wraparound handling as rpi_elp_cam.py update_gate_from_rgb():
    const int lo_h = h - tol.h;
    const int hi_h = h + tol.h;
    std::vector<std::pair<int, int>> ranges;
    if (lo_h < 0) {
        ranges = {{0, hi_h}, {lo_h + 180, 179}};
    } else if (hi_h > 179) {
        ranges = {{lo_h, 179}, {0, hi_h - 180}};
    } else {
        ranges = {{lo_h, hi_h}};
    }

    out.h_ranges.clear();
    // Python filters on the RAW pair (`if b >= a`) and clamps afterwards;
    // keep the same order so every reachable input produces the same ranges.
    for (const auto& ab : ranges) {
        if (ab.second < ab.first) continue;
        const int a = ab.first < 0 ? 0 : ab.first;
        const int b = ab.second > 179 ? 179 : ab.second;
        out.h_ranges.push_back({a, b});
    }

    out.s_lo = s - tol.s < 0 ? 0 : s - tol.s;
    out.s_hi = s + tol.s > 255 ? 255 : s + tol.s;
    out.v_lo = v - tol.v < 0 ? 0 : v - tol.v;
    out.v_hi = v + tol.v > 255 ? 255 : v + tol.v;
    out.enabled = !out.h_ranges.empty();
    return true;
}

int gate_angle(int cx, int cy, int cx0, int cy0) {
    // math.degrees(x) in CPython is x * (180/pi) — a single multiplication.
    // Keep the same operation order so borderline frame values round the same.
    const double rad2deg = 180.0 / 3.14159265358979323846;
    const double deg = std::atan2(static_cast<double>(cy0 - cy),
                                  static_cast<double>(cx - cx0)) * rad2deg;
    // Python: int(deg % 360). Python % always returns a value with the sign of
    // the divisor, i.e. [0, 360) here; replicate with fmod + correction.
    double m = std::fmod(deg, 360.0);
    if (m < 0.0) m += 360.0;
    if (m >= 360.0) m -= 360.0;  // paranoia, cannot happen after the correction
    return static_cast<int>(m);  // truncation == floor for m >= 0
}

int angle_diff_wrapped(int a, int b) {
    int d = a - b;
    if (d < 0) d = -d;
    d %= 360;
    if (d > 180) d = 360 - d;
    return d;
}

SerialDecision decide_serial(const SerialGateState& st, bool found, int angle,
                             double area, double now) {
    SerialDecision d;
    if (found) {
        const bool can_send =
            !st.has_last_sent_angle ||
            (angle_diff_wrapped(angle, st.last_sent_angle) >=
                 kAngleChangeThreshold &&
             now - st.last_send_time >= kMinSendInterval);
        if (can_send) {
            d.action = SerialAction::SendAngle;
            d.angle = angle;
            d.area = static_cast<int>(area);  // Python int(area): truncate
            if (d.area < 0) d.area = 0;
        }
    } else {
        if (st.last_sent_found) {
            d.action = SerialAction::SendLost;
        }
    }
    return d;
}

void commit_sent_angle(SerialGateState& st, int angle, double now) {
    st.has_last_sent_angle = true;
    st.last_sent_angle = angle;
    st.last_send_time = now;
    st.last_sent_found = true;
}

void commit_sent_lost(SerialGateState& st) {
    st.last_sent_found = false;
    st.has_last_sent_angle = false;
}

bool heartbeat_due(double last_heartbeat, double now) {
    return now - last_heartbeat >= kHeartbeatInterval;
}

std::string format_gate_line(int gate_id, int angle, int area) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G%d,%d,%d\n", gate_id, angle, area);
    return std::string(buf);
}

std::string format_lost_line(int gate_id) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "G%d,-1,0\n", gate_id);
    return std::string(buf);
}

}  // namespace pi_core
