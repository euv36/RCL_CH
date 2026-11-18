#include <Arduino.h>
#include "motors.hpp"
#include "config.hpp"

int baseSpeed = BS;

// Time of the last non-zero motor command, used to detect a real standstill
unsigned long lastMotorCommandMicros = 0;

bool motorsIdle() {
  return micros() - lastMotorCommandMicros > 300000UL;
}

constexpr float MotorsAngleSIN = sin(120 * 0.017453);

// Scales all four wheel speeds proportionally so that the strongest one
// never exceeds MotorPWM. Direction and rotation are kept.
void limitPWM(int& leftFront, int& leftBack, int& rightFront, int& rightBack) {
  int maxValue = abs(leftFront);
  maxValue = max(maxValue, abs(leftBack));
  maxValue = max(maxValue, abs(rightFront));
  maxValue = max(maxValue, abs(rightBack));
  if (maxValue > MotorPWM) {
    leftFront = leftFront * MotorPWM / maxValue;
    leftBack = leftBack * MotorPWM / maxValue;
    rightFront = rightFront * MotorPWM / maxValue;
    rightBack = rightBack * MotorPWM / maxValue;
  }
}

// Applies the per-wheel minimum PWM (deadband compensation). Called after
// limitPWM(), because the floor must not be scaled down again.
int applyMinPWM(int speed, int minPWM) {
  if (speed == 0 || minPWM <= 0) {
    return speed;
  }
  minPWM = constrain(minPWM, 0, 255);
  return (speed > 0) ? max(speed, minPWM) : min(speed, -minPWM);
}

void drive(int leftFront, int leftBack, int rightFront, int rightBack) {
  // Per-wheel direction correction (see MotorInvert[] in config.hpp)
  if (MotorInvert[0]) leftFront = -leftFront;
  if (MotorInvert[1]) leftBack = -leftBack;
  if (MotorInvert[2]) rightFront = -rightFront;
  if (MotorInvert[3]) rightBack = -rightBack;

  // Limiting speed
  leftFront = constrain(leftFront, -255, 255);
  leftBack = constrain(leftBack, -255, 255);
  rightFront = constrain(rightFront, -255, 255);
  rightBack = constrain(rightBack, -255, 255);
  limitPWM(leftFront, leftBack, rightFront, rightBack);
  leftFront = applyMinPWM(leftFront, MotorMinPWM[0]);
  leftBack = applyMinPWM(leftBack, MotorMinPWM[1]);
  rightFront = applyMinPWM(rightFront, MotorMinPWM[2]);
  rightBack = applyMinPWM(rightBack, MotorMinPWM[3]);
  if (leftFront != 0 || leftBack != 0 || rightFront != 0 || rightBack != 0) {
    lastMotorCommandMicros = micros();
  }
  // Left front motor
  if (leftFront >= 0) {
    digitalWrite(MotorLF1, LOW);
    analogWrite(MotorLF2, leftFront);
  } else {
    digitalWrite(MotorLF2, LOW);
    analogWrite(MotorLF1, -leftFront);
  }
  // Left back motor
  if (leftBack >= 0) {
    digitalWrite(MotorLB1, LOW);
    analogWrite(MotorLB2, leftBack);
  } else {
    digitalWrite(MotorLB2, LOW);
    analogWrite(MotorLB1, -leftBack);
  }
  // Right front motor
  if (rightFront >= 0) {
    digitalWrite(MotorRF1, LOW);
    analogWrite(MotorRF2, rightFront);
  } else {
    digitalWrite(MotorRF2, LOW);
    analogWrite(MotorRF1, -rightFront);
  }
  // Right back motor
  if (rightBack >= 0) {
    digitalWrite(MotorRB1, LOW);
    analogWrite(MotorRB2, rightBack);
  } else {
    digitalWrite(MotorRB2, LOW);
    analogWrite(MotorRB1, -rightBack);
  }
}

void driveAngle(float angle, int bs) {
  // Calculate
  float k1 = sin((45 - angle) * 0.017453);
  float k2 = sin((45 + angle) * 0.017453);
  // Send
  drive(bs * k2, bs * k1, bs * k1, bs * k2);
}

void driveAngleWithRotation(int angle, int angularSpeed, int bs) {
  float k1 = sin((45 - angle) * 0.017453);
  float k2 = sin((45 + angle) * 0.017453);
  drive(bs * k2 + angularSpeed, bs * k1 + angularSpeed, bs * k1 - angularSpeed, bs * k2 - angularSpeed);
}

void Motor::drive(int speed) {
  speed = constrain(speed, -MotorPWM, MotorPWM);
  if (speed > 0) {
    digitalWrite(pinA, LOW);
    analogWrite(pinB, speed);
  } else if (speed < 0) {
    digitalWrite(pinB, LOW);
    analogWrite(pinA, -speed);
  } else {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
  }
}
