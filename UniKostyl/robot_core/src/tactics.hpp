#pragma once
// tactics.hpp — ALL rcj_arduino_src/controller.cpp tactics as a NON-BLOCKING
// state machine (PROMPT_ROBOT §3): play() (FORWARD/GOALKEEPER roles),
// alignBallGate, catchBall, score, findGate, driveFromLine (+checkLine
// preemption), followBallWithRotation, turnToBall, goalkeeper variants.
//
// The reference implemented the tactics as BLOCKING while-loops with
// drivePD/driveDirection calls inside (e.g. `drivePD(target, 100, speed)` for
// 100 ms). The FSM keeps the same control laws and exit conditions, but each
// blocking loop becomes a state advanced once per fixed-rate tick (default
// 100 Hz); wheel commands are produced every tick and streamed to the Mega.
// Blockingly-timed segments (1.5 s goalkeeper defend, 500 ms short defend,
// 30 s line escape cap, 3 s turn cap) are timeout-bounded states.
//
// Deviations from the reference are marked "DEVIATION" in tactics.cpp and
// summarized in README.md (gates-absent degradation, blind-drive guards,
// frame-composition switch — all required by the "no bugs" rules of
// PROMPT_ROBOT §7). Everything else is a 1:1 port.

#include "angles.hpp"
#include "ball_logic.hpp"
#include "line_logic.hpp"
#include "gate_status.hpp"
#include "heading_ctrl.hpp"
#include "omni.hpp"
#include "drive_limits.hpp"
#include "config.hpp"

namespace robot_core {

enum class TacticsState {
    kIdle,             // motors stopped (standby / degraded / telemetry-stale)
    kTurnToBase,       // play(): |gyro| > 10 -> turn back to baseAngle
    kAlignBallGate,    // alignBallGate()
    kCatchBall,        // catchBall()
    kScore,            // score() (+ kick at the end)
    kFindGate,         // findGate() (goalkeeper cycle entry)
    kTurnToBall,       // turnToBall() (in-place, goalkeeper)
    kGkDefendShort,    // _goalkeeper_ defend window (<=500 ms)
    kGkClassicDefend,  // goalkeeper() classic defend window (<=1.5 s)
    kGkClassicHome,    // goalkeeper() classic "going home"
    kLineEscapeDrive,  // checkLine()/driveFromLine(): drive away (<=30 s)
    kLineEscapeWait,   // driveFromLine(): stopped, wait until the line clears
    kFollowBall,       // followBallWithRotation() (ball-only fallback / demo)
};

const char* tactics_state_name(TacticsState s);

struct SensorInputs {
    unsigned long now_ms = 0;
    Heading heading;              // gyro telemetry (drift-freeze applied)
    bool heading_still = false;
    bool telemetry_fresh = false; // Mega frames older than telemetry_stale_ms -> false
    const LineView* line = nullptr;         // updated line view (raw -> angle done)
    const BallView* ball = nullptr;         // ball snapshot (locator + catcher bit)
    const GateStatusView* gates = nullptr;  // parsed camera status (may be null)
    bool gates_fresh = false;
};

struct DriveOutput {
    Wheels wheels;
    bool kicker_fire = false;   // one-tick request; the Mega owns pulse+cooldown
    bool dribbler_on = false;
};

struct TacticsTransition {
    TacticsState from;
    TacticsState to;
    const char* reason;
    unsigned long now_ms;
};

class TacticsFSM {
public:
    explicit TacticsFSM(const RobotConfig& cfg);

    // Transition sink (tests + on-Pi logging). May be null. `reason` and the
    // state names are static strings.
    void set_logger(void (*log_fn)(void* user, const TacticsTransition& t), void* user);

    DriveOutput tick(const SensorInputs& in);

    TacticsState state() const { return state_; }
    float base_angle() const { return base_angle_.raw_value(); }
    int base_speed() const { return base_speed_; }
    unsigned long transitions() const { return transition_count_; }
    unsigned long kicker_requests() const { return kicker_requests_; }
    unsigned long degrades() const { return degrades_; }

private:
    RobotConfig cfg_;
    HeadingGains gains_;
    HeadingPI pi_;   // driveDirection-style PI law (persistent integral, rcj
                     // static); unit-tested here and reserved for the phase-2
                     // Novice wall legs (driveForward-style holds)
    HeadingPD pd_;   // drivePD-style PD (shared; target re-set per state/tick)

    static HeadingGains gains_from(const RobotConfig& cfg);

    TacticsState state_ = TacticsState::kIdle;
    unsigned long transition_count_ = 0;
    unsigned long kicker_requests_ = 0;
    unsigned long degrades_ = 0;

    void (*log_fn_)(void*, const TacticsTransition&) = nullptr;
    void* log_user_ = nullptr;

    // ---- shared context (rcj globals) ----
    Heading base_angle_;         // rcj baseAngle (captured at setup end)
    bool have_base_angle_ = false;
    int base_speed_ = 0;         // rcj baseSpeed mutable global (BS/LS switch)

    // ---- per-state working data ----
    struct {
        // TurnToBase (rcj turnToAngle): direction fixed from the entry delta.
        Heading goal;
        int direction = 0;
        unsigned long start_ms = 0;
        // Align (rcj correctAngle captured at tactic entry).
        int align_correct = 0;
        bool align_needs_entry = true;
        // Line escape (rcj driveFromLine): heading captured at trigger.
        Heading escape_target;
        TacticsState resume = TacticsState::kIdle;
        // TurnToBall: absolute target composed at entry.
        Heading turn_target;
        bool turn_needs_entry = true;
        // GK defend windows.
        unsigned long defend_start_ms = 0;
        int gk_correct = 0;
        // CatchBall lost-ball guard timer (separate from start_ms, which the
        // escape states own).
        unsigned long catch_lost_since_ms = 0;
        // followBallWithRotation pd(): rcj keeps a function-static lastError
        // for the LIFETIME of the process (never reset on re-entry).
        bool follow_first = true;
        int follow_last_error = 0;
    } st_;

    // Frame composition helper: rcj drivePD(target) treats its argument as an
    // ABSOLUTE gyro heading, and alignBallGate/catchBall/score/findGate pass
    // ball/gate bearings straight in — only correct while the robot heading
    // stays near zero (play() re-heads to baseAngle to enforce that). Kept as
    // the DEFAULT (faithful port); `bearing_targets_relative` (config) composes
    // bearing + current heading instead (the composition turnToBall uses).
    Heading bearing_to_target(float bearing_deg, const SensorInputs& in) const;

    Wheels drive_pd_forward(int angular, int speed);
    void enter_state(TacticsState next, const char* reason, unsigned long now_ms);

    // rcj play() entry (runs at the start of every play() call):
    // |gyro| > 10 -> turnToAngle(baseAngle) : alignBallGate().
    void play_entry(const SensorInputs& in, const char* reason);

    // checkLine() preemption: enter the line escape when a circle triggered.
    bool maybe_line_escape(const SensorInputs& in);

    // Per-state ticks (rcj tactic bodies).
    DriveOutput tick_idle(const SensorInputs& in);
    DriveOutput tick_turn_to_base(const SensorInputs& in);
    DriveOutput tick_align(const SensorInputs& in);
    DriveOutput tick_catch(const SensorInputs& in);
    DriveOutput tick_score(const SensorInputs& in);
    DriveOutput tick_find_gate(const SensorInputs& in);
    DriveOutput tick_turn_to_ball(const SensorInputs& in);
    DriveOutput tick_gk_defend_short(const SensorInputs& in);
    DriveOutput tick_gk_classic_defend(const SensorInputs& in);
    DriveOutput tick_gk_classic_home(const SensorInputs& in);
    DriveOutput tick_line_escape_drive(const SensorInputs& in);
    DriveOutput tick_line_escape_wait(const SensorInputs& in);
    DriveOutput tick_follow_ball(const SensorInputs& in);

    // Gate selection: camera role wins (Idea 9), legacy slot id falls back
    // (rcj had a single "the gate" = the camera's selected target).
    const GateInput* attack_gate(const SensorInputs& in) const;
    const GateInput* own_gate(const SensorInputs& in) const;
};

}  // namespace robot_core
