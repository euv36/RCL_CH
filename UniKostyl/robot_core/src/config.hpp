#pragma once
// config.hpp — all ported constants and tunables in one place.
//
// Sources (READ-ONLY references):
//   rcj_arduino_src/controller.hpp — heading gains, BallTH
//   rcj_arduino_src/motors.hpp     — BS / LS vector speeds
//   rcj_arduino_src/config.hpp     — MotorPWM cap, MotorMinPWM, MotorInvert (Mega)
//   rcj_arduino_src/line.hpp       — per-sensor threshold tables (used here)
//   rcj_arduino_src/ball.hpp       — locator constants (in ball_logic.hpp)
//   PROMPT_ROBOT §7                — tick rate, watchdog, CPU budget

namespace robot_core {

enum class Role { kForward, kGoalkeeper };
enum class GoalkeeperVariant { kDefense /* _goalkeeper_ (what play() runs) */, kClassic /* goalkeeper() */ };

struct RobotConfig {
    // ---- role (rcj config.hpp #define FORWARD / GOALKEEPER) ----
    Role role = Role::kForward;
    GoalkeeperVariant gk_variant = GoalkeeperVariant::kDefense;
    // rcj RCJ_NEW.ino ran followBallWithRotation() in loop() with play()
    // commented out; true = the real play()/goalkeeper tactic, false = the
    // sketch's demo follow mode.
    bool play_mode = true;

    // ---- speeds (rcj motors.hpp: vector magnitudes, not wheel PWM) ----
    int bs = 100;  // base speed (default for driveAngle*/driveDirection)
    int ls = 60;   // slow speed (close to the ball: alignBallGate/catchBall)

    // ---- Mega-side safety mirror (values duplicated from rcj config.hpp; the
    // Mega firmware keeps its own authoritative copies) ----
    int motor_pwm_cap = 70;         // MotorPWM
    int motor_min_pwm[4] = {0, 0, 0, 0};  // MotorMinPWM (LF, LB, RF, RB)
    // MotorInvert = {1,1,0,1} stays Mega-only (physical wiring).

    // ---- heading laws (rcj controller.hpp) ----
    float heading_kp = 2.0f;          // PWM per deg
    float heading_ki = 1.0f;          // PWM per (deg*s)  [controller.hpp; AGENTS quotes 1.5]
    float heading_kd = 1.05f;         // PWM per (deg/s)
    int heading_invert = 1;           // verify with headingSignTest on hardware
    float heading_max_integral = 30.0f;
    float heading_max_share = 0.5f;   // correction cap as share of base wheel PWM

    // ---- tactics thresholds (rcj controller.cpp / config.hpp) ----
    int ball_th = 550;             // BallTH: "ball close" locator distance
    int score_area = 2000;         // score(): gate area to shoot at
    int find_gate_area = 3000;     // findGate(): gate reached
    int gk_home_area = 1500;       // goalkeeper classic: "going home" area
    int align_offset_deg = 60;     // alignBallGate correctAngle (±60)
    int align_tolerance_deg = 5;   // alignBallGate exit |angle| < 5
    int catch_tolerance_deg = 10;  // catchBall window |angle| < 10
    int attack_gate_id = 1;        // score(): camera slot to attack (legacy
                                   // "the gate" = the selected target = G1);
                                   // a slot with camera role "opponent" wins
    int own_gate_id = 2;           // findGate/goalkeeper home: slot to return
                                   // to; a slot with role "ours" wins
    int turn_torque_cap = 30;      // turnToBall in-place torque cap
    int turn_tolerance_deg = 5;    // turnToBall exit |error| < 5
    int turn_base_timeout_ms = 3000;  // play(): re-heading time cap
    float turn_base_angle_limit = 10.0f;  // play(): |gyro| > 10 -> turn back
    unsigned long line_escape_ms = 30000;  // driveFromLine(): drive away cap
    unsigned long gk_defend_ms = 1500;     // goalkeeper classic defend window
    unsigned long gk_defend_short_ms = 500;  // _goalkeeper_ defend window

    // ---- followBallWithRotation pd() gains (rcj controller.hpp p/d) ----
    float follow_p = 0.5f;
    float follow_d = 4.0f;

    // ---- split-specific tunables (PROMPT_ROBOT §6/§7) ----
    int tick_hz = 100;                        // tactics tick (50..100 Hz allowed)
    unsigned long telemetry_stale_ms = 300;   // no Mega telemetry -> degrade
    unsigned long camera_stale_ms = 1000;     // no camera status -> ball-only
    unsigned long ball_lost_abort_ms = 800;   // locator lost -> abort to follow
    bool bearing_targets_relative = false;    // false = rcj frame quirk (see
                                              // tactics.cpp); true = compose
                                              // bearings with the current heading
    bool dribbler_on_hold = true;             // dribbler bit during ball-hold states
};

// Clamp the tick rate to the 50..100 Hz band the split mandates.
int clamp_tick_hz(int hz);

}  // namespace robot_core
