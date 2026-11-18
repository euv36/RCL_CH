#pragma once

extern int baseSpeed;

// Wheel speeds are raw PWM values 0..255 (NOT percent):
//   50 ≈ 20% duty (≈2.4V at 12V), 127 ≈ 50% (≈6V).
// All of them are still capped by MotorPWM in config.hpp.
// NOTE: these are vector magnitudes, not wheel PWM. The actual wheel PWM is
// BS * sin(45°) ≈ 0.71 * BS for a straight move, so BS = 100 gives ≈70 PWM
// per wheel. Raise together with MotorPWM (config.hpp) if the robot is weak.
const int BS = 100;  // base speed: default for driveAngle()/driveAngleWithRotation()
const int LS = 60;   // slow speed: close to the ball (alignBallGate/catchBall)
const int CS = 50;   // not used anywhere (reserved)

void drive(int leftFront, int leftBack, int rightFront, int rightBack);
void driveAngle(float angle, int bs = baseSpeed);
void driveAngleWithRotation(int angle, int angularSpeed, int bs = baseSpeed);
bool motorsIdle();

struct Motor {
  const int pinA, pinB;
  int currentSpeed;
  int targetSpeed;
  Motor(int a, int b) : pinA(a), pinB(b){}

  void drive(int speed);
  // void calculate
};