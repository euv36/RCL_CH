#include "ball_logic.hpp"
#include "angles.hpp"
#include <cmath>

namespace robot_core {

namespace {

// Port of rcj Ball::countAngle(): tmpAngle = 5*raw; 1275 = sentinel (no ball);
// >180 -> -360 (see header note 1: full wrap, identical on the valid domain).
bool angle_from_raw(uint8_t angle_raw, int& out_deg, int& out_raw5) {
    int tmp = 5 * (int)angle_raw;
    out_raw5 = tmp;
    if (tmp == 1275) {
        return false;  // rcj sentinel: found = false
    }
    out_deg = wrap180((float)tmp);  // rcj: single (tmp > 180) -= 360
    return true;
}

// Port of rcj Ball::countTangentAngle(), with the asin domain guard (header
// note 2): tangent = asin(min(1, R/dist)) in degrees; sign flipped when the
// bearing is positive (rcj: if angle > 0 -> negate).
int tangent_from_distance(int angle_deg, int distance) {
    if (distance <= 0) {
        return 0;  // no usable geometry yet
    }
    double ratio = (double)kBallCircleRadius / (double)distance;
    if (ratio > 1.0) ratio = 1.0;
    double asin_deg = std::asin(ratio) / 0.017453;  // rcj constant
    double tangent = asin_deg;
    if (angle_deg > 0) {
        tangent = -tangent;
    }
    return (int)tangent;
}

}  // namespace

bool Ball::is_calm(unsigned long now_ms) const {
    // rcj isCalm(): (millis() - calmTime > 5000) — the bearing has been
    // stable (jumps <= 10 raw) for 5 s.
    if (!have_calm_time_) {
        return false;
    }
    return now_ms - calm_time_ms_ > 5000UL;
}

void Ball::apply_sample(const LocatorSample& s, unsigned long now_ms) {
    // Strength first (rcj updateStatus order: strength, angle, tangent,
    // distance, catcher) — both must agree on found_.
    int dist = distance_;
    bool found = false;
    if (s.strength != 0) {
        dist = kReferenceDistance / (int)s.strength;  // rcj countDistance
        found = true;
    }

    int angle = angle_;
    int raw5 = raw_angle_;
    bool angle_found = angle_from_raw(s.angle_raw, angle, raw5);
    if (!angle_found) {
        found = false;
    } else {
        // rcj: a raw jump > 10 means the ball moved -> reset the calm window.
        if (!have_calm_time_ || std::abs(raw5 - raw_angle_) > 10) {
            calm_time_ms_ = now_ms;
            have_calm_time_ = true;
        }
    }

    angle_ = angle;
    raw_angle_ = raw5;
    strength_ = s.strength;
    distance_ = found ? dist : distance_;  // keep last distance when lost
    found_ = found;
    if (found) {
        tangent_angle_ = tangent_from_distance(angle_, distance_);
    }
}

void Ball::update(unsigned long now_ms) {
    LocatorSample s;
    uint8_t b = 0;
    s.ok = true;
    if (!transport_.read_register(kRegAngle, b)) {
        s.ok = false;
    } else {
        s.angle_raw = b;
    }
    if (s.ok && !transport_.read_register(kRegStrength, b)) s.ok = false;
    else if (s.ok) s.strength = b;
    if (s.ok && !transport_.read_register(kRegAngleClose, b)) s.ok = false;
    else if (s.ok) s.angle_close_raw = b;
    if (s.ok && !transport_.read_register(kRegStrengthClose, b)) s.ok = false;
    else if (s.ok) s.strength_close = b;

    if (!s.ok) {
        // I2C failure (Locator absent/busy): stale-value policy with timeout —
        // never block the control loop on I2C (PROMPT_ROBOT §7).
        if (have_ok_ && now_ms - last_ok_ms_ > stale_timeout_ms_) {
            stale_ = true;
            found_ = false;
            last_error_ = "locator i2c stale";
        } else if (!have_ok_) {
            stale_ = true;
            found_ = false;
            last_error_ = "locator i2c failed";
        }
        return;
    }
    last_ok_ms_ = now_ms;
    have_ok_ = true;
    stale_ = false;
    last_error_ = "";
    apply_sample(s, now_ms);
}

}  // namespace robot_core
