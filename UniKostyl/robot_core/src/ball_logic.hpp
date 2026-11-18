#pragma once
// ball_logic.hpp — IR ball Locator semantics, ported from
// rcj_arduino_src/ball.cpp + ball.hpp (READ-ONLY reference).
//
// The Locator chip (I2C address 0x0E) MOVED to the Pi's I2C bus in the split;
// this module is pure logic: one I2C-semantics interface (IBallTransport) is
// injected, so tests drive it with synthetic registers and the Linux i2c-dev
// backend (locator_i2c_linux.cpp) plugs in on the Pi.
//
// Register semantics (rcj ball.hpp Locator):
//   0x04 angle         raw bearing byte, expected range 0..72 = 0..360 deg
//                      (the code multiplies by 5), 255 = no ball sentinel
//                      (5*255 = 1275)
//   0x05 strength      0 = no ball; distance = 10000 / strength
//   0x06 angleClose    close-range bearing (same encoding) — read, not used
//                      by the reference tactics
//   0x07 strengthClose close-range strength — read, not used by the tactics
//
// FAITHFUL-PORT NOTES (documented deviations, all no-ops on the valid domain):
//   1. rcj countAngle() does a single `if (tmpAngle > 180) tmpAngle -= 360;`.
//      For the documented raw domain 0..72 (tmpAngle 0..360) that is exactly a
//      wrap to -180..180; this port wraps fully (identical results on the
//      valid domain, defensive for garbage inputs).
//   2. rcj countTangentAngle() computes asin(BallCircleRadius/distance)
//      without a domain guard: distance < BallCircleRadius (300) — reachable
//      when the ball is at the dribbler (distance = 10000/strength, strength
//      > 33) — yields NaN and UB in the float->int conversion. This port
//      clamps the argument to 1.0 (tangent saturates at 90 deg, same sign
//      rule). Documented deviation, required by the "no bugs" rule.
//   3. I2C failures keep the last values only for `stale_timeout_ms` (rcj had
//      no failure handling at all); after the timeout the ball is "not found"
//      and the reason is exposed via last_error().

#include <cstdint>
#include <cstddef>

namespace robot_core {

constexpr int kReferenceDistance = 10000;  // rcj ball.hpp ReferenceDistance
constexpr int kHoldBallRadius = 500;       // rcj HoldBallRadius (unused by tactics, kept)
constexpr int kBallCircleRadius = 300;     // rcj BallCircleRadius

constexpr uint8_t kLocatorAddress = 0x0E;  // rcj Locator adr
constexpr uint8_t kRegAngle = 0x04;
constexpr uint8_t kRegStrength = 0x05;
constexpr uint8_t kRegAngleClose = 0x06;
constexpr uint8_t kRegStrengthClose = 0x07;

// One I2C transaction set against the Locator, expressed as the rcj
// single-register reads (the real backend issues the same register writes +
// 1-byte reads; a simulated backend just fills the struct).
struct LocatorSample {
    uint8_t angle_raw = 0;         // register 0x04
    uint8_t strength = 0;          // register 0x05
    uint8_t angle_close_raw = 0;   // register 0x06
    uint8_t strength_close = 0;    // register 0x07
    bool ok = false;               // all reads succeeded
};

// Injectable I2C semantics (rcj Locator::readAngle/readStrength/...):
// implementations issue register-select writes + 1-byte reads.
class IBallTransport {
public:
    virtual ~IBallTransport() = default;
    // Read one Locator register byte. Returns false on I2C failure
    // (NACK / bus busy / absent device). Must not block for long.
    virtual bool read_register(uint8_t reg, uint8_t& out) = 0;
};

// Absent-device backend (--no-i2c bench mode): every read fails, so the Ball
// stays not-found/stale and the tactics degrade to ball-less modes.
class NullBallTransport : public IBallTransport {
public:
    bool read_register(uint8_t, uint8_t&) override { return false; }
};

class Ball {
public:
    explicit Ball(IBallTransport& transport) : transport_(transport) {}

    // One sensor update: the four register reads + the rcj count*() pipeline.
    // `now_ms` timestamps the update for the calm/stale policies.
    void update(unsigned long now_ms);

    // --- rcj Ball getters (semantics unchanged) ---
    int get_angle() const { return angle_; }            // -180..180 bearing (deg)
    int get_raw_angle() const { return raw_angle_; }    // 0..1275 raw (5*byte)
    int get_strength() const { return strength_; }
    int get_distance() const { return distance_; }      // 10000/strength
    int get_tangent_angle() const { return tangent_angle_; }
    bool is_found() const { return found_; }
    bool is_catched() const { return catched_; }
    // rcj isCalm(): no bearing jump > 10 deg for 5000 ms.
    bool is_calm(unsigned long now_ms) const;

    // Catcher microswitch input (moved from rcj digitalRead(14) to the Mega
    // telemetry bit — same meaning).
    void set_catcher(bool closed) { catched_ = closed; }

    // Stale-value policy (PROMPT_ROBOT §7): transport failures keep the last
    // values for this long, then the ball reads as not found.
    void set_stale_timeout_ms(unsigned long ms) { stale_timeout_ms_ = ms; }
    bool is_stale() const { return stale_; }
    const char* last_error() const { return last_error_; }

private:
    IBallTransport& transport_;
    int angle_ = 0;
    int raw_angle_ = 0;
    int tangent_angle_ = 0;
    int strength_ = 0;
    int distance_ = kReferenceDistance;
    bool found_ = false;
    bool catched_ = false;
    unsigned long calm_time_ms_ = 0;
    bool have_calm_time_ = false;
    unsigned long last_ok_ms_ = 0;
    bool have_ok_ = false;
    bool stale_ = false;
    const char* last_error_ = "";
    unsigned long stale_timeout_ms_ = 200;

    // rcj countAngle()/countStrength() helpers, kept as free functions of the
    // update pipeline for clarity.
    void apply_sample(const LocatorSample& s, unsigned long now_ms);
};

// Immutable snapshot of a Ball at a moment — what the fixed-rate tick and the
// simulator consume (no shared mutable state; the locator thread fills a live
// Ball under a mutex and the tick copies this view).
struct BallView {
    int angle = 0;
    int raw_angle = 0;
    int tangent_angle = 0;
    int strength = 0;
    int distance = kReferenceDistance;
    bool found = false;
    bool catched = false;
    bool calm = false;
    bool stale = false;
};

inline BallView ball_view(const Ball& b, unsigned long now_ms) {
    BallView v;
    v.angle = b.get_angle();
    v.raw_angle = b.get_raw_angle();
    v.tangent_angle = b.get_tangent_angle();
    v.strength = b.get_strength();
    v.distance = b.get_distance();
    v.found = b.is_found();
    v.catched = b.is_catched();
    v.calm = b.is_calm(now_ms);
    v.stale = b.is_stale();
    return v;
}

}  // namespace robot_core
