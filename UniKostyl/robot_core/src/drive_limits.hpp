#pragma once
// drive_limits.hpp — Pi-side mirror of the Mega drive safety path.
//
// The Mega (mega_fw/src/motors_fw.cpp) applies, in order:
//   MotorInvert[] -> constrain ±255 -> limitPWM (proportional, max = MotorPWM)
//   -> applyMinPWM floor (AFTER the cap on purpose: the floor may exceed it).
// The Pi mirrors constrain + limitPWM + applyMinPWM so that:
//   - the commands it reasons about (maxCorrection share of base wheel) are the
//     PWM values actually applied, and
//   - the Mega's own application of the same path is idempotent (a no-op),
//     keeping the Mega authoritative for safety.
// MotorInvert[] is deliberately NOT applied here (Mega-only, physical wiring).

namespace robot_core {

struct SafetyParams {
    int motor_pwm_cap = 70;              // rcj config.hpp MotorPWM (raw PWM, not %)
    int min_pwm[4] = {0, 0, 0, 0};       // rcj MotorMinPWM[4], order LF LB RF RB
};

// Apply constrain ±255 -> limitPWM -> applyMinPWM to the four logical wheel
// values (in place). Order LF, LB, RF, RB.
void apply_drive_safety(int& lf, int& lb, int& rf, int& rb, const SafetyParams& p);

// Base wheel PWM for a straight cardinal move at vector speed `speed`
// (rcj driveDirection(): baseWheel = (int)(speed * 0.707f)).
int base_wheel_pwm(int speed);

// Largest heading correction as a share of the base wheel PWM, minimum 1
// (rcj: maxCorrection = (int)(baseWheel * headingMaxShare); if < 1 then 1).
int max_heading_correction(int speed, float heading_max_share);

}  // namespace robot_core
