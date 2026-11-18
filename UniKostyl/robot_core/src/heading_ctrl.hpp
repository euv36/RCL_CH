#pragma once
// heading_ctrl.hpp — the heading-hold laws, moved OFF the Mega and closed on
// the gyro telemetry stream (the split's core change): the Mega streams
// heading at ~100 Hz; these step functions run inside the fixed-rate tactics
// tick and produce the rotation term for the omni mixing.
//
// Ports of rcj_arduino_src/controller.cpp driveDirection() (PI law) and
// drivePD()/turnToBall() (PD law), with the gains of controller.hpp:
//   headingKp=2.0 PWM/deg, headingKi=1.0 PWM/(deg*s), headingKd=1.05 PWM/(deg/s),
//   headingInvert=1, headingMaxIntegral=30, headingMaxShare=0.5.
// (controller.hpp is the source of truth for the gains; the older AGENTS.md
// quotes Ki=1.5 — the shipped code has 1.0.)
//
// Both laws share the yaw-rate estimator of the reference: the rate is
// measured over >=20 ms windows (the DMP yaw updates at 100 Hz; per-iteration
// rates are noise) and low-passed 0.5/0.5. Because the tick feeds samples at
// a fixed rate, dt is derived from the caller's timestamps, not measured.

namespace robot_core {

struct HeadingGains {
    float kp = 2.0f;
    float ki = 1.0f;
    float kd = 1.05f;
    int invert = 1;               // verify with the headingSignTest on hardware
    float max_integral = 30.0f;   // anti-windup clamp (deg*s)
    float max_share = 0.5f;       // correction cap as a share of base wheel PWM
};

// Yaw-rate estimator ported from the reference's inline code: rate over >=20 ms
// windows of wrapped heading deltas, low-passed 0.5*old + 0.5*new.
class RateEstimator {
public:
    // Feed one telemetry sample; returns the current (filtered) rate estimate.
    // now_ms must be non-decreasing.
    float update(unsigned long now_ms, float heading_deg);
    bool have_rate() const { return have_rate_; }
    void reset();

private:
    bool have_rate_ = false;
    float last_heading_ = 0.0f;
    unsigned long last_ms_ = 0;
    float yaw_rate_ = 0.0f;
};

// PI heading hold — port of driveDirection(). The controller captures the
// heading to hold via capture(); each step() produces the clamped angular
// speed. The integral is PERSISTENT across captures (the reference kept it in
// a function static: one wheel asymmetry pulls the same way every segment).
class HeadingPI {
public:
    explicit HeadingPI(const HeadingGains& gains = HeadingGains()) : gains_(gains) {}

    // Capture the heading to hold. `speed` sets the correction budget
    // (maxCorrection = share of base wheel PWM of this speed).
    void capture(float heading_deg, int speed);

    // One control step. heading_deg = latest telemetry sample. Returns the
    // clamped angular speed (PWM units) for drive_angle_with_rotation().
    int step(unsigned long now_ms, float heading_deg);

    float integral() const { return integral_; }
    float error() const { return last_error_; }

private:
    HeadingGains gains_;
    float heading_target_ = 0.0f;
    int speed_ = 0;
    int max_correction_ = 1;
    float integral_ = 0.0f;
    float last_error_ = 0.0f;
    unsigned long last_step_ms_ = 0;
    RateEstimator rate_;
};

// PD approach law — port of drivePD()/turnToBall(): no integral, torque =
// invert * (kp*error - kd*rate), clamped to maxCorrection.
class HeadingPD {
public:
    explicit HeadingPD(const HeadingGains& gains = HeadingGains()) : gains_(gains) {}

    // Capture an ABSOLUTE heading target (the tactic composes the bearing into
    // the right frame before calling; see tactics.cpp bearing_to_target()).
    void set_target(float target_deg, int speed, int torque_cap = 0);

    // One control step; returns the clamped angular speed.
    int step(unsigned long now_ms, float heading_deg);

    float error() const { return last_error_; }
    float target() const { return target_; }

private:
    HeadingGains gains_;
    float target_ = 0.0f;
    int speed_ = 0;
    int max_correction_ = 1;
    float last_error_ = 0.0f;
    RateEstimator rate_;
};

}  // namespace robot_core
