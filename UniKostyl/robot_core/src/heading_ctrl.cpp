#include "heading_ctrl.hpp"
#include "angles.hpp"
#include "drive_limits.hpp"
#include <cstdlib>
#include <algorithm>

namespace robot_core {

static int clamp_int(int v, int lo, int hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// ---------------- RateEstimator ----------------

float RateEstimator::update(unsigned long now_ms, float heading_deg) {
    if (last_ms_ == 0) {
        last_heading_ = heading_deg;
        last_ms_ = now_ms;
        return yaw_rate_;
    }
    float dt = (now_ms - last_ms_) / 1000.0f;
    if (dt < 0.02f) {
        // Reference: rates over windows shorter than 20 ms are noise (DMP yaw
        // updates at 100 Hz). Keep accumulating; report the last filtered rate.
        return yaw_rate_;
    }
    float rate = heading_diff(heading_deg, last_heading_) / dt;
    yaw_rate_ = 0.5f * yaw_rate_ + 0.5f * rate;
    last_heading_ = heading_deg;
    last_ms_ = now_ms;
    have_rate_ = true;
    return yaw_rate_;
}

void RateEstimator::reset() {
    have_rate_ = false;
    last_heading_ = 0.0f;
    last_ms_ = 0;
    yaw_rate_ = 0.0f;
}

// ---------------- HeadingPI (driveDirection port) ----------------

void HeadingPI::capture(float heading_deg, int speed) {
    // Reference: driveDirection() sends one command before reading the heading,
    // so it holds the REAL current heading, not a stale frozen value. On the
    // split the tick always feeds fresh telemetry, so capturing the current
    // sample reproduces that intent.
    heading_target_ = heading_deg;
    speed_ = speed;
    max_correction_ = max_heading_correction(speed, gains_.max_share);
    // NOTE: the integral is deliberately NOT reset — the reference kept the
    // heading integral static between drive calls (a wheel asymmetry pulls in
    // the same direction in every ~1 s segment; re-learning it from zero made
    // a square slowly rotate).
    last_error_ = 0.0f;
}

int HeadingPI::step(unsigned long now_ms, float heading_deg) {
    // Wrapped to -180..180 (reference: GyroAngle::operator-).
    float error = heading_diff(heading_target_, heading_deg);
    last_error_ = error;
    float yaw_rate = rate_.update(now_ms, heading_deg);

    // dt for the integral, from the caller's timestamps (a fixed-rate tick);
    // guarded against startup (last_step_ms_ == 0) and absurd gaps.
    float dt = 0.01f;
    if (last_step_ms_ != 0 && now_ms > last_step_ms_) {
        dt = (now_ms - last_step_ms_) / 1000.0f;
        if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;  // rcj: absurd gap -> do not compensate
    }
    last_step_ms_ = now_ms;

    float raw_correction = gains_.kp * error + gains_.ki * integral_ - gains_.kd * yaw_rate;
    float correction = gains_.invert * raw_correction;
    int angular_speed = clamp_int((int)correction, -max_correction_, max_correction_);

    // Anti-windup: integrate only while the correction was not saturated, so a
    // saturated step does not poison the next iterations (reference law).
    if (std::abs(correction) < (float)max_correction_) {
        integral_ += error * dt;
        if (integral_ > gains_.max_integral) integral_ = gains_.max_integral;
        if (integral_ < -gains_.max_integral) integral_ = -gains_.max_integral;
    }
    (void)speed_;
    return angular_speed;
}

// ---------------- HeadingPD (drivePD / turnToBall port) ----------------

void HeadingPD::set_target(float target_deg, int speed, int torque_cap) {
    target_ = target_deg;
    speed_ = speed;
    max_correction_ = max_heading_correction(speed, gains_.max_share);
    if (torque_cap > 0) {
        // turnToBall caps the in-place torque at ±30 PWM regardless of speed.
        max_correction_ = torque_cap;
    }
    last_error_ = 0.0f;
}

int HeadingPD::step(unsigned long now_ms, float heading_deg) {
    float error = heading_diff(target_, heading_deg);
    last_error_ = error;
    float yaw_rate = rate_.update(now_ms, heading_deg);
    float torque = gains_.kp * error - gains_.kd * yaw_rate;
    int angular_speed = clamp_int((int)(gains_.invert * torque),
                                  -max_correction_, max_correction_);
    return angular_speed;
}

}  // namespace robot_core
