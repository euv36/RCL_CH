// sim.hpp — simulator harness for the tactics FSM (PROMPT_ROBOT §8: "PC unit
// tests with a ball/lines/gates simulation (no hardware)").
//
// Pure header, no IO. Models, all deterministic (no noise unless asked):
//   SimRobot  — planar kinematics: omni wheels (post-safety PWM) -> forward
//               speed + yaw rate; position/heading integrate per step.
//               NOTE: MotorInvert is NOT applied here (the sim robot has
//               "correct" wiring: logical +PWM = forward; the Mega applies
//               invert on the real robot).
//   SimGyro   — heading telemetry source: sim heading (+ optional drift).
//   SimBall   — static/held ball position -> Locator register bytes fed
//               through the REAL Ball logic (FakeTransport + ball_view()).
//   SimLines  — field walls -> per-sensor raw ADC values through the REAL
//               line_update() (FORWARD table).
//   SimGates  — gate world positions -> camera status JSON in the Idea 2
//               schema parsed by the REAL parse_gate_status(). The emitted
//               camera angle models the rcj camera mount (+90), which
//               camera_angle_to_robot() compensates (-90) — the same pair of
//               conventions as the reference gate.cpp.
//   SimMega   — the executor side of the binary link AS A SPEC MODEL: runs
//               Pi→Mega frames through the SAME pure scanner/decoders as
//               mega_fw/src/link_rx.cpp, applies the safety path (reuses
//               apply_drive_safety), enforces the 300 ms watchdog (motors stop)
//               and the kicker pulse+cooldown. The real firmware itself is
//               verified by PlatformIO compile + mega_fw/tests.
//   SimWorld  — ties everything: step(dt_ms, DriveOutput) advances physics,
//               then sensor_inputs() builds the FSM's SensorInputs.

#pragma once
#include "angles.hpp"
#include "ball_logic.hpp"
#include "line_logic.hpp"
#include "gate_status.hpp"
#include "omni.hpp"
#include "drive_limits.hpp"
#include "heading_ctrl.hpp"
#include "tactics.hpp"
#include "link_codec.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace robot_core_sim {

using robot_core::Heading;
using robot_core::wrap360;

// ---------------- SimRobot ----------------

struct SimRobot {
    float x = 90.0f;        // cm, field frame (0..W)
    float y = 60.0f;        // cm
    float theta = 0.0f;     // deg world (0 = +x, CCW)
    float drift = 0.0f;     // deg/s gyro bias (SimGyro)

    // Plant gains (arbitrary but sane: PWM 70 all-around ≈ 40 cm/s; full-spin
    // differential 70 ≈ 180 deg/s).
    static constexpr float kV = 0.57f;   // cm/s per PWM
    static constexpr float kW = 2.57f;   // deg/s per PWM differential

    int wheels[4] = {0, 0, 0, 0};        // last applied (logical) wheels
    bool kicker_pulsing = false;
    bool dribbler_on = false;

    void step(float dt_s) {
        float v = ((wheels[0] + wheels[1] + wheels[2] + wheels[3]) / 4.0f) * kV;
        float w = ((wheels[0] + wheels[1]) - (wheels[2] + wheels[3])) / 2.0f * kW;
        theta = wrap360(theta + w * dt_s);
        float rad = theta * 0.017453f;
        x += v * std::cos(rad) * dt_s;
        y += v * std::sin(rad) * dt_s;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
};

// ---------------- field / gates ----------------

struct FieldDef {
    float w = 180.0f;   // cm (rules: 110-160 x 180-225; walled)
    float h = 120.0f;
    float wall_margin = 20.0f;  // cm from a wall -> that side's sensors fire
    // Gates (inner face positions):
    float own_x = 0.0f, own_y = 60.0f;        // "ours" (yellow side)
    float opp_x = 180.0f, opp_y = 60.0f;      // "opponent" (blue side)
};

// ---------------- SimBall ----------------

// Feeds Locator register bytes derived from the world state through the REAL
// Ball pipeline.
class FakeTransport : public robot_core::IBallTransport {
public:
    uint8_t angle_raw = 255;      // 255 -> 1275 sentinel (not found)
    uint8_t strength = 0;
    bool fail = false;            // transport failure (stale policy tests)

    bool read_register(uint8_t reg, uint8_t& out) override {
        if (fail) return false;
        switch (reg) {
            case robot_core::kRegAngle: out = angle_raw; return true;
            case robot_core::kRegStrength: out = strength; return true;
            case robot_core::kRegAngleClose: out = angle_raw; return true;
            case robot_core::kRegStrengthClose: out = strength; return true;
        }
        return false;
    }
};

// ---------------- SimMega (spec model of the executor) ----------------

class SimMega {
public:
    explicit SimMega(SimRobot& robot) : robot_(robot) {}

    // Feeds binary wire bytes through the SAME pure scanner/decoders used by
    // the real Mega seam (framing conformance; the firmware itself is
    // compile+host-tested only). Partial frames are fine: the scanner keeps
    // state across calls.
    void feed_frame(const std::string& frame) {
        robot_core::FrameScanner::Frame f;
        for (unsigned char c : frame) {
            if (scanner_.push(c, f) != robot_core::FrameScanner::Result::Frame) {
                continue;
            }
            if (f.type == robot_core::kTypeStop) {
                apply_stop();
            } else {
                robot_core::DriveBits cmd;
                if (robot_core::decode_drive_frame(f.raw, f.len, cmd)) {
                    apply_drive(cmd);
                }
            }
        }
    }

    void request_kicker() {
        if (robot_.kicker_pulsing) return;
        if (now_ms < cooldown_end_ms) return;
        robot_.kicker_pulsing = true;
        pulse_end_ms = now_ms + 40;
        kicks++;
    }

    // Advance the executor state (watchdog + pulse timing). dt in ms.
    void step(unsigned long dt) {
        now_ms += dt;
        if (robot_.kicker_pulsing && now_ms >= pulse_end_ms) {
            robot_.kicker_pulsing = false;
            cooldown_end_ms = now_ms + 1000;
        }
        if (fed_ && now_ms - last_cmd_ms > 300) {
            for (int i = 0; i < 4; i++) robot_.wheels[i] = 0;
            robot_.kicker_pulsing = false;
            robot_.dribbler_on = false;
            watchdog_tripped = true;
        }
    }

    SimRobot& robot_;
    unsigned long now_ms = 0;
    unsigned long last_cmd_ms = 0;
    bool fed_ = false;
    unsigned long pulse_end_ms = 0;
    unsigned long cooldown_end_ms = 0;
    unsigned long kicks = 0;
    bool watchdog_tripped = false;

private:
    void apply_stop() {
        for (int i = 0; i < 4; i++) robot_.wheels[i] = 0;
        robot_.kicker_pulsing = false;
        robot_.dribbler_on = false;
        feed_watchdog();
    }

    void apply_drive(const robot_core::DriveBits& cmd) {
        // Mega safety path: invert (Mega-owned) + constrain/cap/floor.
        // The sim robot models CORRECT wiring, so the invert identity is
        // kept as a no-op here with the array documented in PROTOCOL.md.
        int w[4] = {cmd.pwm[0], cmd.pwm[1], cmd.pwm[2], cmd.pwm[3]};
        robot_core::SafetyParams sp;  // Mega defaults: cap 70, no floor
        robot_core::apply_drive_safety(w[0], w[1], w[2], w[3], sp);
        for (int i = 0; i < 4; i++) robot_.wheels[i] = w[i];
        robot_.dribbler_on = cmd.dribbler_on;
        // Kicker: pulse 40 ms, cooldown 1000 ms after the pulse (rcj).
        if (cmd.kicker_fire) request_kicker();
        feed_watchdog();
    }

    void feed_watchdog() {
        last_cmd_ms = now_ms;
        fed_ = true;
        watchdog_tripped = false;
    }

    robot_core::FrameScanner scanner_;
};

// ---------------- SimWorld ----------------

class SimWorld {
public:
    SimWorld(const robot_core::RobotConfig& cfg, FieldDef field)
        : cfg_(cfg), field_(field) {
        // Ball starts in front of the robot (bearing ~0) so the default view
        // is found/straight; tests move it.
        ball_x = robot.x + 40;
        ball_y = robot.y;
    }

    SimRobot robot;
    FieldDef field_;
    const robot_core::RobotConfig& cfg_;

    float ball_x = 0, ball_y = 0;
    bool ball_present = true;
    bool ball_held = false;       // once caught, glued to the robot front
    bool transport_fail = false;

    robot_core::Ball ball{transport_};
    FakeTransport transport_;

    unsigned long now_ms = 0;
    unsigned long last_cmd_ms = 0;

    // --- world -> locator registers ---
    void update_ball_registers() {
        if (!ball_present) {
            transport_.angle_raw = 255;   // sentinel -> 1275
            transport_.strength = 0;
            return;
        }
        if (ball_held) {
            // Glued to the robot front (dribbler zone).
            float rad = robot.theta * 0.017453f;
            ball_x = robot.x + std::cos(rad) * 12.0f;
            ball_y = robot.y + std::sin(rad) * 12.0f;
        }
        float dx = ball_x - robot.x;
        float dy = ball_y - robot.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        float bearing = wrap360((float)std::atan2(dy, dx) / 0.017453f - robot.theta);
        // Locator raw byte: 0..72 = 0..360 deg in 5-deg steps; strength = 10000/dist.
        transport_.angle_raw = (uint8_t)std::lround(bearing / 5.0f);
        if (transport_.angle_raw > 72) transport_.angle_raw = 72;
        int s = (dist > 0.5f) ? (int)(10000.0f / dist) : 0;
        if (dist > 1500.0f) s = 0;   // out of locator range
        if (s > 255) s = 255;
        transport_.strength = (uint8_t)s;
    }

    // --- world -> camera status JSON (Idea 2 schema, lenient parser-tested) ---
    static float gate_area(float dist_cm) {
        if (dist_cm < 1.0f) dist_cm = 1.0f;
        float a = 2000000.0f / (dist_cm * dist_cm);  // reaches 2000 @ ~32 cm
        if (a > 60000.0f) a = 60000.0f;
        return a;
    }

    std::string gate_status_json() {
        char buf[512];
        float d_own = std::sqrt((robot.x - field_.own_x) * (robot.x - field_.own_x) +
                                (robot.y - field_.own_y) * (robot.y - field_.own_y));
        float d_opp = std::sqrt((robot.x - field_.opp_x) * (robot.x - field_.opp_x) +
                                (robot.y - field_.opp_y) * (robot.y - field_.opp_y));
        // Camera bearing of each gate (robot frame, 0..360, 0 = robot's right,
        // CCW), with the rcj +90 mount offset that camera_angle_to_robot()
        // removes.
        float phi_own = wrap360((float)std::atan2(field_.own_y - robot.y,
                                                  field_.own_x - robot.x) / 0.017453f
                                - robot.theta);
        float phi_opp = wrap360((float)std::atan2(field_.opp_y - robot.y,
                                                  field_.opp_x - robot.x) / 0.017453f
                                - robot.theta);
        float cam_own = wrap360(phi_own + 90.0f);
        float cam_opp = wrap360(phi_opp + 90.0f);
        std::snprintf(buf, sizeof(buf),
                      "{\"gates\":{"
                      "\"1\":{\"enabled\":true,\"found\":true,\"angle\":%.1f,"
                      "\"area\":%d,\"role\":\"opponent\"},"
                      "\"2\":{\"enabled\":true,\"found\":true,\"angle\":%.1f,"
                      "\"area\":%d,\"role\":\"ours\"}"
                      "},\"fps\":25.9}",
                      cam_opp, (int)gate_area(d_opp),
                      cam_own, (int)gate_area(d_own));
        return std::string(buf);
    }

    // --- world -> line raw values ---
    void fill_line_raw(int raw[16]) {
        const int* th = robot_core::line_thresholds_forward;
        // Pair directions (deg, robot frame): rcj weightSet1 angles.
        static const float pair_dir[8] = {0, 45, 90, 135, 180, 225, 270, 315};
        // Distance to the field wall along each robot-frame direction.
        float wall_pair[8];
        for (int p = 0; p < 8; p++) {
            float rad = (robot.theta + pair_dir[p]) * 0.017453f;
            float cdx = std::cos(rad), cdy = std::sin(rad);
            float t = 1e9f;
            if (cdx > 1e-4f) t = std::min(t, (field_.w - robot.x) / cdx);
            if (cdx < -1e-4f) t = std::min(t, (0.0f - robot.x) / cdx);
            if (cdy > 1e-4f) t = std::min(t, (field_.h - robot.y) / cdy);
            if (cdy < -1e-4f) t = std::min(t, (0.0f - robot.y) / cdy);
            wall_pair[p] = t;
        }
        for (int i = 0; i < 16; i++) {
            int p = i / 2;
            bool on = wall_pair[p] < field_.wall_margin;
            raw[i] = on ? th[i] + 200 : th[i] - 200;
            if (raw[i] < 0) raw[i] = 0;
            if (raw[i] > 1023) raw[i] = 1023;
        }
    }

    // --- advance the world by dt_ms with the FSM's drive output ---
    robot_core::SensorInputs step(unsigned long dt_ms,
                                  const robot_core::DriveOutput& out) {
        now_ms += dt_ms;
        // Apply the logical wheels directly (the sim robot has correct wiring;
        // the Mega invert + safety is a hardware concern modeled in SimMega).
        robot.wheels[0] = out.wheels.lf;
        robot.wheels[1] = out.wheels.lb;
        robot.wheels[2] = out.wheels.rf;
        robot.wheels[3] = out.wheels.rb;
        if (out.kicker_fire) robot.kicker_pulsing = true;
        robot.dribbler_on = out.dribbler_on;
        robot.step(dt_ms / 1000.0f);

        // Ball update through the REAL pipeline at the sim tick. The dribbler
        // holds the ball once the catcher microswitch closes (auto-glue).
        if (!ball_held && ball_present && near_ball()) {
            ball_held = true;
        }
        update_ball_registers();
        transport_.fail = transport_fail;
        ball.set_catcher(ball_held || near_ball());
        ball.update(now_ms);

        return build_inputs();
    }

    bool near_ball() const {
        float dx = ball_x - robot.x, dy = ball_y - robot.y;
        return (dx * dx + dy * dy) < 144.0f;  // <12 cm -> microswitch closes
    }

    robot_core::SensorInputs build_inputs() {
        robot_core::SensorInputs in;
        in.now_ms = now_ms;
        in.heading = Heading(robot.theta + robot.drift * (now_ms / 1000.0f));
        in.heading_still = false;
        in.telemetry_fresh = true;
        static int raw[16];
        fill_line_raw(raw);
        line_update(line, raw, robot_core::line_thresholds_forward);
        in.line = &line;
        in.ball = &bv;
        bv = robot_core::ball_view(ball, now_ms);
        status_json = gate_status_json();
        parse_gate_status(status_json, now_ms, gates);
        in.gates = &gates;
        in.gates_fresh = gate_status_fresh(gates, now_ms, 1000);
        return in;
    }

    robot_core::LineView line;
    robot_core::BallView bv;
    robot_core::GateStatusView gates;
    std::string status_json;
};

}  // namespace robot_core_sim
