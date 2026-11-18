#pragma once
#include <Arduino.h>

// motors_fw — the drive safety path, ported 1:1 from rcj_arduino_src/motors.cpp
// (drive(), limitPWM(), applyMinPWM(), motorsIdle()).
//
// The mixing (driveAngle/driveAngleWithRotation) deliberately does NOT live
// here anymore: the Pi computes the four wheel PWMs. The Mega only applies
// MotorInvert[] -> ±255 constrain -> proportional MotorPWM cap -> MotorMinPWM
// floor, then drives the H-bridges. This path stays authoritative for safety.

// Drives the four wheels with raw (logical, pre-invert) PWM values.
// Order everywhere: leftFront, leftBack, rightFront, rightBack.
void drive(int leftFront, int leftBack, int rightFront, int rightBack);

// All four wheels to 0 (still runs through the safety path; zero stays zero).
void motorsStop();

// True when no non-zero wheel command was applied for >300 ms. Feeds the gyro
// drift-freeze logic exactly like the reference (motorsIdle()).
bool motorsIdle();

// Time of the last non-zero motor command (micros()), for diagnostics.
extern unsigned long lastMotorCommandMicros;
