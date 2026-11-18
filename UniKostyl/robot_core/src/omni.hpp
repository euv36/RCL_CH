#pragma once
// omni.hpp — omni drive mixing: motion angle/speed/rotation -> 4 wheel PWMs.
//
// Ported from rcj_arduino_src/motors.cpp driveAngle()/driveAngleWithRotation()
// (READ-ONLY reference). Truncation toward zero per wheel matches the
// reference's implicit float->int conversion.
//
// Motion-angle convention (rcj controller.cpp): 0 = forward, 180 = backward,
// +90 / -90 = sideways (swap if left/right are mirrored on the robot).
// Wheel order everywhere: LF, LB, RF, RB. Values are LOGICAL (pre-invert):
// MotorInvert[] is applied by the Mega safety path, never here.

namespace robot_core {

struct Wheels {
    int lf = 0, lb = 0, rf = 0, rb = 0;
};

// Straight omni move at motion `angle` deg with base speed `bs` (vector
// magnitude, not wheel PWM: a cardinal move gives bs*0.707 per wheel).
Wheels drive_angle(float angle, int bs);

// Same + in-place rotation term: +rot on the left wheels, -rot on the right
// wheels (rcj driveAngleWithRotation).
Wheels drive_angle_with_rotation(float angle, int rot, int bs);

// Pure in-place spin (rcj controller.cpp turn patterns: drive(rot, rot, -rot, -rot)).
Wheels spin_in_place(int rot);

}  // namespace robot_core
