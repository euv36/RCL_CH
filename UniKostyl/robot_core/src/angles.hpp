#pragma once
// angles.hpp — angle wrap helpers and the Heading value type.
//
// Pure logic (no IO). Conventions match the reference firmware
// (UniKostyl/rcj_arduino_src):
//   - headings live in -180..180 ("raw value"), with a 0..360 view ("value");
//   - every difference of two headings is wrapped to -180..180, so 359 -> 1
//     is 2 degrees, not 358 (PROMPT_EN §2.3).
//   - camera gate angles are 0..360, 0° = right, CCW (PROMPT_EN §4).

namespace robot_core {

// Wrap any angle to -180..180 (exclusive of exactly -180 handled: -180 maps
// to 180 like the reference GyroAngle arithmetic).
float wrap180(float a);

// Wrap any angle to 0..360.
float wrap360(float a);

// Smallest signed difference target-current, in -180..180. This is the wrap
// the reference implemented via GyroAngle::operator-.
float heading_diff(float target, float current);

// The Heading type mirrors the reference GyroAngle: raw -180..180 plus a
// 0..360 view, wrapped +/- operators. Kept as a plain struct (no operator
// overloads beyond arithmetic the tactics actually use) to stay testable.
struct Heading {
    float raw = 0.0f;  // -180..180

    Heading() = default;
    explicit Heading(float raw_deg) : raw(wrap180(raw_deg)) {}

    // 0..360 view (reference GyroAngle::value()).
    float value() const { return (raw < 0.0f) ? raw + 360.0f : raw; }
    float raw_value() const { return raw; }

    void operator+=(Heading other) { raw = wrap180(raw + other.raw); }
    void operator-=(Heading other) { raw = wrap180(raw - other.raw); }
};

inline Heading operator-(Heading lhs, Heading rhs) {
    Heading r;
    r.raw = wrap180(lhs.raw - rhs.raw);
    return r;
}

inline Heading operator+(Heading lhs, Heading rhs) {
    Heading r;
    r.raw = wrap180(lhs.raw + rhs.raw);
    return r;
}

// Degrees to radians with the SAME truncated constant the reference mixing
// uses (0.017453, not M_PI/180) — kept bit-comparable with rcj_arduino_src
// driveAngle()/countTangentAngle().
constexpr float kDeg2Rad = 0.017453f;

}  // namespace robot_core
