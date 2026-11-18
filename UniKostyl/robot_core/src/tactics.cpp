#include "tactics.hpp"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace robot_core {

// rcj hardcoded approach constants (kept as named literals of the port).
static const int kAlignCloseDistance = 600;  // alignBallGate: dist < 600 -> LS
static const int kGkApproachOffsetDeg = 30;  // goalkeeper(): correctAngle ±30
static const int kTurnBaseSpeed = 45;        // play(): turnToAngle(baseAngle, 45, ...)
static const float kTurnBaseGap = 3.0f;      // play(): turnToAngle gap
static const float kTurnBaseCorrection = 10.0f;  // play(): turnToAngle correction
static const unsigned long kTurnToBallCapMs = 3000;  // rcj turnToBall: 3 s

const char* tactics_state_name(TacticsState s) {
    switch (s) {
        case TacticsState::kIdle: return "IDLE";
        case TacticsState::kTurnToBase: return "TURN_TO_BASE";
        case TacticsState::kAlignBallGate: return "ALIGN_BALL_GATE";
        case TacticsState::kCatchBall: return "CATCH_BALL";
        case TacticsState::kScore: return "SCORE";
        case TacticsState::kFindGate: return "FIND_GATE";
        case TacticsState::kTurnToBall: return "TURN_TO_BALL";
        case TacticsState::kGkDefendShort: return "GK_DEFEND_SHORT";
        case TacticsState::kGkClassicDefend: return "GK_CLASSIC_DEFEND";
        case TacticsState::kGkClassicHome: return "GK_CLASSIC_HOME";
        case TacticsState::kLineEscapeDrive: return "LINE_ESCAPE_DRIVE";
        case TacticsState::kLineEscapeWait: return "LINE_ESCAPE_WAIT";
        case TacticsState::kFollowBall: return "FOLLOW_BALL";
    }
    return "?";
}

HeadingGains TacticsFSM::gains_from(const RobotConfig& cfg) {
    HeadingGains g;
    g.kp = cfg.heading_kp;
    g.ki = cfg.heading_ki;
    g.kd = cfg.heading_kd;
    g.invert = cfg.heading_invert;
    g.max_integral = cfg.heading_max_integral;
    g.max_share = cfg.heading_max_share;
    return g;
}

TacticsFSM::TacticsFSM(const RobotConfig& cfg)
    : cfg_(cfg), pi_(gains_from(cfg)), pd_(gains_from(cfg)) {
    base_speed_ = cfg_.bs;
}

void TacticsFSM::set_logger(void (*log_fn)(void*, const TacticsTransition&), void* user) {
    log_fn_ = log_fn;
    log_user_ = user;
}

Heading TacticsFSM::bearing_to_target(float bearing_deg, const SensorInputs& in) const {
    if (cfg_.bearing_targets_relative) {
        // Correct frame composition (what rcj turnToBall already does).
        return in.heading + Heading(bearing_deg);
    }
    // rcj frame: the bearing is used as an absolute DMP-frame target. Only
    // equivalent while the robot heading stays near zero — play() enforces
    // that by re-heading to baseAngle. Faithful default (see README).
    return Heading(bearing_deg);
}

// Final Pi-side safety mirror of the Mega drive path (idempotent there).
static Wheels finish_safety(Wheels w, const RobotConfig& cfg) {
    SafetyParams sp;
    sp.motor_pwm_cap = cfg.motor_pwm_cap;
    for (int i = 0; i < 4; i++) sp.min_pwm[i] = cfg.motor_min_pwm[i];
    apply_drive_safety(w.lf, w.lb, w.rf, w.rb, sp);
    return w;
}

// rcj drivePD body per call: driveAngleWithRotation(0, angularSpeed, speed).
Wheels TacticsFSM::drive_pd_forward(int angular, int speed) {
    return finish_safety(drive_angle_with_rotation(0.0f, angular, speed), cfg_);
}

void TacticsFSM::enter_state(TacticsState next, const char* reason, unsigned long now_ms) {
    if (next == state_) {
        return;
    }
    TacticsTransition t{state_, next, reason, now_ms};
    state_ = next;
    transition_count_++;

    // Entry actions (rcj tactic-entry code).
    switch (next) {
        case TacticsState::kTurnToBase:
            st_.direction = 0;  // computed on the first tick from the entry delta
            st_.start_ms = now_ms;
            break;
        case TacticsState::kAlignBallGate:
            st_.align_needs_entry = true;
            break;
        case TacticsState::kCatchBall:
            base_speed_ = cfg_.ls;  // rcj catchBall(): baseSpeed = LS
            st_.catch_lost_since_ms = 0;
            break;
        case TacticsState::kTurnToBall:
            st_.start_ms = now_ms;
            st_.turn_needs_entry = true;
            break;
        case TacticsState::kGkDefendShort:
        case TacticsState::kGkClassicDefend:
            st_.defend_start_ms = now_ms;
            break;
        case TacticsState::kFollowBall:
            // rcj pd() keeps its function-static lastError for the whole
            // process lifetime — deliberately NOT reset here.
            break;
        default:
            break;
    }
    if (log_fn_) {
        log_fn_(log_user_, t);
    }
}

// ---------------- gate selection ----------------

const GateInput* TacticsFSM::attack_gate(const SensorInputs& in) const {
    if (!in.gates_fresh || !in.gates) {
        return nullptr;
    }
    for (int id = 1; id <= 3; id++) {
        if (in.gates->gates[id].configured && in.gates->gates[id].role == GateRole::kOpponent) {
            return &in.gates->gates[id];
        }
    }
    int id = cfg_.attack_gate_id;
    if (id < 1 || id > 3) id = 1;
    return in.gates->gates[id].configured ? &in.gates->gates[id] : nullptr;
}

const GateInput* TacticsFSM::own_gate(const SensorInputs& in) const {
    if (!in.gates_fresh || !in.gates) {
        return nullptr;
    }
    for (int id = 1; id <= 3; id++) {
        if (in.gates->gates[id].configured && in.gates->gates[id].role == GateRole::kOurs) {
            return &in.gates->gates[id];
        }
    }
    int id = cfg_.own_gate_id;
    if (id < 1 || id > 3) id = 2;
    return in.gates->gates[id].configured ? &in.gates->gates[id] : nullptr;
}

// ---------------- checkLine() preemption ----------------

bool TacticsFSM::maybe_line_escape(const SensorInputs& in) {
    if (state_ == TacticsState::kLineEscapeDrive || state_ == TacticsState::kLineEscapeWait) {
        return false;
    }
    if (!in.line) {
        return false;
    }
    // rcj checkLine(): react on the two sensor circles.
    if (!in.line->first_circle && !in.line->second_circle) {
        return false;
    }
    st_.resume = state_;
    // rcj driveFromLine(): drivePD(GyroAngle(line.getAngle() + 180), 30000, BS)
    // — the heading is captured ONCE at trigger time and held by the PD.
    st_.escape_target = Heading((float)in.line->angle + 180.0f);
    st_.start_ms = in.now_ms;
    enter_state(TacticsState::kLineEscapeDrive, "line triggered", in.now_ms);
    return true;
}

// ---------------- main tick ----------------

DriveOutput TacticsFSM::tick(const SensorInputs& in) {
    // rcj setup(): baseAngle captured once after calibration.
    if (!have_base_angle_ && in.telemetry_fresh) {
        base_angle_ = in.heading;
        have_base_angle_ = true;
    }

    // Safety pause: no telemetry from the Mega -> stop sending commands (the
    // Mega watchdog is the actual stop; the Pi must not stack new commands on
    // a dead link). PROMPT_ROBOT §7: degraded modes, never block. DEVIATION:
    // the reference had no such state (single-board).
    if (!in.telemetry_fresh) {
        degrades_++;
        enter_state(TacticsState::kIdle, "telemetry stale", in.now_ms);
        DriveOutput out;
        return out;  // zeros
    }

    // checkLine() preemption — ran inside every rcj blocking loop, so it
    // preempts everything except the escape states themselves.
    maybe_line_escape(in);

    switch (state_) {
        case TacticsState::kIdle: return tick_idle(in);
        case TacticsState::kTurnToBase: return tick_turn_to_base(in);
        case TacticsState::kAlignBallGate: return tick_align(in);
        case TacticsState::kCatchBall: return tick_catch(in);
        case TacticsState::kScore: return tick_score(in);
        case TacticsState::kFindGate: return tick_find_gate(in);
        case TacticsState::kTurnToBall: return tick_turn_to_ball(in);
        case TacticsState::kGkDefendShort: return tick_gk_defend_short(in);
        case TacticsState::kGkClassicDefend: return tick_gk_classic_defend(in);
        case TacticsState::kGkClassicHome: return tick_gk_classic_home(in);
        case TacticsState::kLineEscapeDrive: return tick_line_escape_drive(in);
        case TacticsState::kLineEscapeWait: return tick_line_escape_wait(in);
        case TacticsState::kFollowBall: return tick_follow_ball(in);
    }
    DriveOutput out;
    return out;
}

// ---------------- Idle / entry ----------------

// rcj loop() called followBallWithRotation() (the sketch's active demo) or
// play() (the real tactic). Config selects which one the FSM runs; kIdle is
// the standby/entry state.
DriveOutput TacticsFSM::tick_idle(const SensorInputs& in) {
    DriveOutput out;
    if (!cfg_.play_mode) {
        enter_state(TacticsState::kFollowBall, "demo mode entry", in.now_ms);
        return out;
    }
    if (cfg_.role == Role::kForward) {
        play_entry(in, "play entry");
        return out;
    }
    if (cfg_.gk_variant == GoalkeeperVariant::kClassic) {
        // rcj goalkeeper(): standby until the ball is close (dist < BallTH).
        const BallView* b = in.ball;
        if (!b) return out;
        if (!b->calm || b->found) {
            if (b->distance < cfg_.ball_th) {
                st_.gk_correct = (b->angle < 0) ? -kGkApproachOffsetDeg
                                                : kGkApproachOffsetDeg;
                enter_state(TacticsState::kGkClassicDefend, "ball close", in.now_ms);
            }
        }
        return out;  // standby zeros (rcj drive(0,0,0,0) each outer iteration)
    }
    // _goalkeeper_ cycle entry: findGate() first.
    enter_state(TacticsState::kFindGate, "gk cycle entry", in.now_ms);
    return out;
}

void TacticsFSM::play_entry(const SensorInputs& in, const char* reason) {
    // rcj play(): if (abs(getGyroAngle().rawValue()) > 10) turnToAngle(baseAngle...)
    if (have_base_angle_ && std::abs(in.heading.raw_value()) > cfg_.turn_base_angle_limit) {
        enter_state(TacticsState::kTurnToBase, reason, in.now_ms);
    } else {
        enter_state(TacticsState::kAlignBallGate, reason, in.now_ms);
    }
}

// ---------------- FORWARD play() chain ----------------

// rcj turnToAngle(baseAngle, 45, gap=3.0, correction=10.0):
//   direction fixed from the entry delta; CW -> goal -= 10, drive(+45 spin);
//   CCW -> goal += 10, drive(-45 spin); exit when |delta| <= 3.
DriveOutput TacticsFSM::tick_turn_to_base(const SensorInputs& in) {
    DriveOutput out;
    if (st_.direction == 0) {
        float delta0 = heading_diff(base_angle_.raw_value(), in.heading.raw_value());
        st_.direction = delta0 >= 0 ? 1 : -1;
        st_.goal = Heading(base_angle_.raw_value() - st_.direction * kTurnBaseCorrection);
        st_.start_ms = in.now_ms;
    }
    float delta = heading_diff(st_.goal.raw_value(), in.heading.raw_value());
    if (std::abs(delta) <= kTurnBaseGap) {
        st_.direction = 0;
        enter_state(TacticsState::kAlignBallGate, "re-headed to base", in.now_ms);
        return out;  // rcj drive(0,0,0,0) on exit
    }
    // DEVIATION: the reference turnToAngle had no time cap (a stuck gyro spins
    // forever); 10x the nominal duration bounds it (PROMPT_ROBOT §7).
    if (in.now_ms - st_.start_ms > (unsigned long)cfg_.turn_base_timeout_ms * 10) {
        st_.direction = 0;
        enter_state(TacticsState::kAlignBallGate, "re-heading timeout", in.now_ms);
        return out;
    }
    out.wheels = finish_safety(spin_in_place(st_.direction * kTurnBaseSpeed), cfg_);
    return out;
}

// rcj alignBallGate():
//   correctAngle = ball.angle < 0 ? -60 : +60   (captured at entry)
//   while (|ball.angle| >= 5) {
//     baseSpeed = dist < 600 ? LS : BS
//     checkLine();
//     if (ball.isFound()) drivePD(GyroAngle(ball.angle + correctAngle), 100, baseSpeed);
//   }
DriveOutput TacticsFSM::tick_align(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    if (!b) return out;
    if (st_.align_needs_entry) {
        st_.align_correct = (b->angle < 0) ? -cfg_.align_offset_deg
                                           : cfg_.align_offset_deg;
        st_.align_needs_entry = false;
    }
    if (std::abs(b->angle) < cfg_.align_tolerance_deg) {
        enter_state(TacticsState::kCatchBall, "ball aligned", in.now_ms);
        return out;
    }
    base_speed_ = (b->distance < kAlignCloseDistance) ? cfg_.ls : cfg_.bs;
    if (!b->found) {
        // rcj skipped the drive when the ball was not found (stood still,
        // kept polling). Same: zero output, state kept.
        return out;
    }
    float target = (float)b->angle + (float)st_.align_correct;
    pd_.set_target(bearing_to_target(target, in).raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    out.dribbler_on = cfg_.dribbler_on_hold;
    return out;
}

// rcj catchBall(): baseSpeed = LS;
//   while (!ball.isCatched() && |ball.angle| < 10) drivePD(GyroAngle(ball.angle), 100, LS);
DriveOutput TacticsFSM::tick_catch(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    if (!b) return out;
    if (b->catched) {
        enter_state(TacticsState::kScore, "ball catched", in.now_ms);
        return out;
    }
    if (std::abs(b->angle) >= cfg_.catch_tolerance_deg) {
        // Ball rolled out of the approach window: rcj exited catchBall() and
        // play() re-entered alignBallGate().
        enter_state(TacticsState::kAlignBallGate, "ball off axis", in.now_ms);
        return out;
    }
    // DEVIATION (blind-drive guard): rcj kept the loop (and the last PD
    // command) while the ball was not found as long as the bearing stayed
    // inside ±10 — a lost ball drove the robot forward on a stale bearing.
    // The port stops after ball_lost_abort_ms of "not found" and re-aligns.
    if (!b->found) {
        if (st_.catch_lost_since_ms == 0) st_.catch_lost_since_ms = in.now_ms;
        if (in.now_ms - st_.catch_lost_since_ms > cfg_.ball_lost_abort_ms) {
            st_.catch_lost_since_ms = 0;
            enter_state(TacticsState::kAlignBallGate, "ball lost in catch", in.now_ms);
            return out;
        }
        return out;  // brief loss: stand still (no stale-bearing drive)
    }
    st_.catch_lost_since_ms = 0;
    pd_.set_target(bearing_to_target((float)b->angle, in).raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    out.dribbler_on = cfg_.dribbler_on_hold;
    return out;
}

// rcj score():
//   while (gate.getArea() < 2000 && ball.isCatched()) drivePD(gate.angle, 1000, BS);
//   kick();
DriveOutput TacticsFSM::tick_score(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    const GateInput* gate = attack_gate(in);
    if (!gate || !gate->has_angle) {
        // DEVIATION (gates-absent degradation, PROMPT_ROBOT §4): with no
        // camera status there is nothing to shoot at; rcj would drive on a
        // stale gate angle forever. Ball-only fallback: follow the ball and
        // log the reason. Never kick blind.
        degrades_++;
        enter_state(TacticsState::kFollowBall, "no gates: score skipped", in.now_ms);
        return out;
    }
    if (b && b->catched && gate->area < cfg_.score_area) {
        // Keep driving at the gate while the ball is held and the gate is far.
        pd_.set_target(bearing_to_target(gate->angle.raw_value(), in).raw_value(), cfg_.bs);
        int angular = pd_.step(in.now_ms, in.heading.raw_value());
        out.wheels = drive_pd_forward(angular, cfg_.bs);
        out.dribbler_on = cfg_.dribbler_on_hold;
        return out;
    }
    // Loop exit (area reached OR ball released): rcj kicks unconditionally
    // after the loop — kept faithful (the catcher switch is the release
    // sensor; a held ball with a briefly-lost locator still shoots).
    out.kicker_fire = true;
    kicker_requests_++;
    play_entry(in, "score done");
    return out;
}

// ---------------- goalkeeper (_goalkeeper_, the variant play() runs) ----------------

// rcj findGate():
//   while (gate.getArea() < 3000 && gate.getArea() > 0) drivePD(gate.angle, 100, baseSpeed);
//   ("GATE FOUND" -> stop)
DriveOutput TacticsFSM::tick_find_gate(const SensorInputs& in) {
    DriveOutput out;
    const GateInput* gate = own_gate(in);
    if (!gate) {
        // DEVIATION (gates-absent): rcj's loop simply did not run (area 0)
        // and the cycle continued; here null gate = absent camera. The cycle
        // continues ball-only (turnToBall/defend need no gates), reason logged.
        degrades_++;
        enter_state(TacticsState::kTurnToBall, "no gates: findGate skipped", in.now_ms);
        return out;
    }
    if (gate->area >= cfg_.find_gate_area || gate->area == 0 || !gate->has_angle) {
        enter_state(TacticsState::kTurnToBall,
                    gate->area >= cfg_.find_gate_area ? "gate found" : "gate absent",
                    in.now_ms);
        return out;
    }
    base_speed_ = cfg_.bs;
    pd_.set_target(bearing_to_target(gate->angle.raw_value(), in).raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    return out;
}

// rcj turnToBall(): up to 3 s in-place PD turn toward current + ball bearing;
// exit |error| < 5; torque capped ±30.
DriveOutput TacticsFSM::tick_turn_to_ball(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    if (!b || !b->found) {
        // DEVIATION (blind-turn guard): rcj composed the target from a stale
        // angle and spun toward garbage; the port skips the turn when there
        // is nothing to turn to.
        enter_state(TacticsState::kGkDefendShort, "no ball: turn skipped", in.now_ms);
        return out;
    }
    if (st_.turn_needs_entry) {
        // Entry composition (rcj: target = current + bearing).
        st_.turn_target = in.heading + Heading((float)b->angle);
        st_.turn_needs_entry = false;
    }
    float error = heading_diff(st_.turn_target.raw_value(), in.heading.raw_value());
    if (std::abs(error) < (float)cfg_.turn_tolerance_deg) {
        enter_state(TacticsState::kGkDefendShort, "turned to ball", in.now_ms);
        return out;
    }
    if (in.now_ms - st_.start_ms > kTurnToBallCapMs) {  // rcj: 3 s cap
        enter_state(TacticsState::kGkDefendShort, "turn timeout", in.now_ms);
        return out;
    }
    pd_.set_target(st_.turn_target.raw_value(), cfg_.bs, cfg_.turn_torque_cap);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = finish_safety(spin_in_place(angular), cfg_);
    return out;
}

// rcj _goalkeeper_ defend window:
//   startTime = millis();
//   while (dist < BallTH && millis() - startTime < 500) drivePD(ball.angle, 50, BS);
//   drive(0,0,0,0); kick();
DriveOutput TacticsFSM::tick_gk_defend_short(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    bool b_ok = b && b->found;
    bool in_window = b_ok &&
                     (in.now_ms - st_.defend_start_ms < cfg_.gk_defend_short_ms) &&
                     b->distance < cfg_.ball_th;
    if (in_window) {
        pd_.set_target(bearing_to_target((float)b->angle, in).raw_value(), cfg_.bs);
        int angular = pd_.step(in.now_ms, in.heading.raw_value());
        out.wheels = drive_pd_forward(angular, cfg_.bs);
        return out;
    }
    // Window done (or ball went away): stop + kick (rcj; cooldown on the Mega).
    out.kicker_fire = true;
    kicker_requests_++;
    enter_state(TacticsState::kFindGate, "gk cycle restart", in.now_ms);
    return out;
}

// ---------------- goalkeeper (classic variant) ----------------

// rcj goalkeeper() inner defend:
//   while (millis() - startTime < 1500) drivePD(ball.angle + correctAngle, 50, baseSpeed);
DriveOutput TacticsFSM::tick_gk_classic_defend(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    if (!b) return out;
    if (in.now_ms - st_.defend_start_ms >= cfg_.gk_defend_ms) {
        // rcj flips correctAngle by the GATE sign here (then does not use it
        // in the home loop — kept as a comment for fidelity).
        enter_state(TacticsState::kGkClassicHome, "defend window over", in.now_ms);
        return out;
    }
    base_speed_ = cfg_.bs;
    float target = (float)b->angle + (float)st_.gk_correct;
    pd_.set_target(bearing_to_target(target, in).raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    return out;
}

// rcj goalkeeper() home:
//   while (|gate.angle| > 10 && gate.area < 1500) drivePD(gate.angle, 100, baseSpeed);
//   then drive(0,0,0,0)
DriveOutput TacticsFSM::tick_gk_classic_home(const SensorInputs& in) {
    DriveOutput out;
    const GateInput* gate = own_gate(in);
    if (!gate || !gate->has_angle) {
        // DEVIATION (gates-absent): rcj would spin on the stale gate angle;
        // the port goes back to standby with the reason logged.
        degrades_++;
        enter_state(TacticsState::kIdle, "no gates: home skipped", in.now_ms);
        return out;
    }
    if (std::abs(gate->angle.raw_value()) <= 10.0f || gate->area >= cfg_.gk_home_area) {
        enter_state(TacticsState::kIdle, "home reached", in.now_ms);
        return out;
    }
    base_speed_ = cfg_.bs;
    pd_.set_target(bearing_to_target(gate->angle.raw_value(), in).raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    return out;
}

// ---------------- line escape (checkLine/driveFromLine) ----------------

// rcj driveFromLine() phase 1:
//   drivePD(GyroAngle(line.getAngle() + 180), 30000, BS);  [target captured once]
//   baseSpeed = BS;
DriveOutput TacticsFSM::tick_line_escape_drive(const SensorInputs& in) {
    DriveOutput out;
    if (in.now_ms - st_.start_ms >= cfg_.line_escape_ms) {
        enter_state(TacticsState::kLineEscapeWait, "escape time cap", in.now_ms);
        return out;
    }
    base_speed_ = cfg_.bs;
    pd_.set_target(st_.escape_target.raw_value(), base_speed_);
    int angular = pd_.step(in.now_ms, in.heading.raw_value());
    out.wheels = drive_pd_forward(angular, base_speed_);
    return out;
}

// rcj driveFromLine() phase 2 (motors already stopped by drivePD's exit):
//   while (line.getAngle() != -1) { line.updateStatus(); }   [busy wait]
DriveOutput TacticsFSM::tick_line_escape_wait(const SensorInputs& in) {
    DriveOutput out;
    if (!in.line || in.line->angle == -1) {
        enter_state(st_.resume, "line cleared", in.now_ms);
        return out;
    }
    return out;  // stay stopped until the line clears
}

// ---------------- followBallWithRotation (demo / ball-only fallback) ----------------

// rcj followBallWithRotation() (debug.cpp):
//   driveAngleWithRotation(ball.getAngle(), pd(ball.getAngle(), p=0.5, d=4.0));
// rcj pd(): static int lastError (process-lifetime); delta = |e|*p + (|e|-last)*d,
// sign preserved. DEVIATION (lost-ball guard): rcj drove on the stale angle;
// the port stops and waits when the ball is not found.
DriveOutput TacticsFSM::tick_follow_ball(const SensorInputs& in) {
    DriveOutput out;
    const BallView* b = in.ball;
    if (!b || !b->found) {
        return out;  // stand still and wait (never drive a stale bearing)
    }
    // rcj pd() body (first call seeds lastError with the SIGNED error).
    int e = b->angle;
    bool sign = e < 0;
    int ae = std::abs(e);
    int last = st_.follow_first ? e : st_.follow_last_error;
    int delta = (int)(ae * cfg_.follow_p + (ae - last) * cfg_.follow_d);
    st_.follow_first = false;
    st_.follow_last_error = ae;
    int rot = sign ? -delta : delta;

    base_speed_ = cfg_.bs;
    out.wheels = finish_safety(drive_angle_with_rotation((float)b->angle, rot, base_speed_),
                               cfg_);
    out.dribbler_on = cfg_.dribbler_on_hold;
    return out;
}

}  // namespace robot_core
