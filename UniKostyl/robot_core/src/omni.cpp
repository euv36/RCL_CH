#include "omni.hpp"
#include "angles.hpp"
#include <cmath>

namespace robot_core {

Wheels drive_angle(float angle, int bs) {
    // rcj motors.cpp driveAngle(): k1 = sin((45 - angle) deg), k2 = sin((45 + angle) deg)
    // drive(bs * k2, bs * k1, bs * k1, bs * k2)
    float k1 = std::sin((45.0f - angle) * kDeg2Rad);
    float k2 = std::sin((45.0f + angle) * kDeg2Rad);
    Wheels w;
    w.lf = (int)(bs * k2);
    w.lb = (int)(bs * k1);
    w.rf = (int)(bs * k1);
    w.rb = (int)(bs * k2);
    return w;
}

Wheels drive_angle_with_rotation(float angle, int rot, int bs) {
    // rcj motors.cpp driveAngleWithRotation():
    // drive(bs*k2 + rot, bs*k1 + rot, bs*k1 - rot, bs*k2 - rot)
    float k1 = std::sin((45.0f - angle) * kDeg2Rad);
    float k2 = std::sin((45.0f + angle) * kDeg2Rad);
    Wheels w;
    w.lf = (int)(bs * k2) + rot;
    w.lb = (int)(bs * k1) + rot;
    w.rf = (int)(bs * k1) - rot;
    w.rb = (int)(bs * k2) - rot;
    return w;
}

Wheels spin_in_place(int rot) {
    Wheels w;
    w.lf = rot;
    w.lb = rot;
    w.rf = -rot;
    w.rb = -rot;
    return w;
}

}  // namespace robot_core
