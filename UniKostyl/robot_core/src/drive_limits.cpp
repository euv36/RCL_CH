#include "drive_limits.hpp"
#include <cstdlib>
#include <algorithm>

namespace robot_core {

static int constrain_int(int v, int lo, int hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// Port of rcj motors.cpp limitPWM: scale all four proportionally so the
// strongest wheel never exceeds the cap. Direction and rotation are kept.
static void limit_pwm(int& lf, int& lb, int& rf, int& rb, int cap) {
    int maxValue = std::abs(lf);
    maxValue = std::max(maxValue, std::abs(lb));
    maxValue = std::max(maxValue, std::abs(rf));
    maxValue = std::max(maxValue, std::abs(rb));
    if (maxValue > cap) {
        lf = lf * cap / maxValue;
        lb = lb * cap / maxValue;
        rf = rf * cap / maxValue;
        rb = rb * cap / maxValue;
    }
}

// Port of rcj motors.cpp applyMinPWM: per-wheel deadband floor, applied after
// the cap so it is never scaled down again.
static int apply_min_pwm(int speed, int min_pwm) {
    if (speed == 0 || min_pwm <= 0) {
        return speed;
    }
    min_pwm = constrain_int(min_pwm, 0, 255);
    return (speed > 0) ? std::max(speed, min_pwm) : std::min(speed, -min_pwm);
}

void apply_drive_safety(int& lf, int& lb, int& rf, int& rb, const SafetyParams& p) {
    lf = constrain_int(lf, -255, 255);
    lb = constrain_int(lb, -255, 255);
    rf = constrain_int(rf, -255, 255);
    rb = constrain_int(rb, -255, 255);
    limit_pwm(lf, lb, rf, rb, p.motor_pwm_cap);
    lf = apply_min_pwm(lf, p.min_pwm[0]);
    lb = apply_min_pwm(lb, p.min_pwm[1]);
    rf = apply_min_pwm(rf, p.min_pwm[2]);
    rb = apply_min_pwm(rb, p.min_pwm[3]);
}

int base_wheel_pwm(int speed) {
    return (int)(speed * 0.707f);
}

int max_heading_correction(int speed, float heading_max_share) {
    int max_correction = (int)(base_wheel_pwm(speed) * heading_max_share);
    if (max_correction < 1) {
        max_correction = 1;
    }
    return max_correction;
}

}  // namespace robot_core
