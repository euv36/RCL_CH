#include "angles.hpp"
#include <cmath>

namespace robot_core {

float wrap180(float a) {
    // Same result as the reference wrapAngle()/GyroAngle arithmetic for every
    // in-domain input; a branch-free-ish full wrap is used so out-of-domain
    // garbage (e.g. 740) cannot escape the range.
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

float wrap360(float a) {
    a = std::fmod(a, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a;
}

float heading_diff(float target, float current) {
    return wrap180(target - current);
}

}  // namespace robot_core
