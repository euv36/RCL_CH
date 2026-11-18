#pragma once
#include "gyro.hpp"
#include "motors.hpp"

void catchBall();
void score();
void findGate();
int medianFilter(int a, int b, int c);
int pd(double error, float p, float d);
void driveFromLine();
void checkLine();
void play();
void alignBallGate();
void catchBall();
void score();
void goalkeeper();
void goalkeeper_();
void _goalkeeper_();
void turnToBall();
void findGate();
void driveDirection(int moveAngle, unsigned long duration, int speed = BS);
void driveForward(unsigned long duration, int speed = BS);
void driveBackward(unsigned long duration, int speed = BS);
void driveLeft(unsigned long duration, int speed = BS);
void driveRight(unsigned long duration, int speed = BS);
void turnInPlace(int angle, int speed);
void drivePD(GyroAngle target, unsigned long duration, int speed);
const float p = 0.5;
const float d = 4.0;
const float gp = 0.5;
const float gd = 4.0;

// Heading-hold (gyro) controller used by driveDirection().
// It does not just steer: it adds/subtracts PWM from the left and right
// wheels so the robot keeps the heading it started with. On the floor the
// wheels have different friction, so this is what makes it go straight.
//   headingKp          : PWM per degree of heading error.
//   headingKi          : PWM per (degree * second) - slowly learns the
//                        constant pull to one side and cancels it. The integral
//                        is kept between drive calls on purpose: it re-learned
//                        from zero in every ~1 s segment, which is why a square
//                        slowly rotated.
//   headingKd          : damping PWM per (deg/s) of the measured yaw rate.
//   headingInvert      : sign of the rotation command, verify with
//                        headingSignTest().
//   headingMaxIntegral : anti-windup clamp for the integral term.
//   headingMaxShare    : largest correction, as a share of the base wheel
//                        PWM (0.5 = 50%, 1.0 = +-70 PWM). Raised so the
//                        controller can overpower a mechanical rightward pull.
const float headingKp = 2.0f;
const float headingKi = 1.0f;
const float headingKd = 1.05f;  // damping PWM per (deg/s) of yaw rate
const int headingInvert = 1;    // -1 if the gyro sees the opposite turn (floor verdict: flipping to -1 made the right arc STRONGER)
const float headingMaxIntegral = 30.0f;
const float headingMaxShare = 0.5f;
const int BallTH = 550;
extern GyroAngle baseAngle;