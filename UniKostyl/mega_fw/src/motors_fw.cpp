#include <Arduino.h>
#include "motors_fw.hpp"
#include "config_fw.hpp"

// Ported from rcj_arduino_src/motors.cpp (READ-ONLY reference) — the safety
// path is kept line-for-line; only the mixing helpers were dropped (they moved
// to the Pi's robot_core).

// Time of the last non-zero motor command, used to detect a real standstill
// (gyro drift-freeze coupling + diagnostics).
unsigned long lastMotorCommandMicros = 0;

bool motorsIdle() {
  return micros() - lastMotorCommandMicros > 300000UL;
}

// Scales all four wheel speeds proportionally so that the strongest one
// never exceeds MotorPWM. Direction and rotation are kept.
static void limitPWM(int& leftFront, int& leftBack, int& rightFront, int& rightBack) {
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
static int applyMinPWM(int speed, int minPWM) {
  if (speed == 0 || minPWM <= 0) {
    return speed;
  }
  minPWM = constrain(minPWM, 0, 255);
  return (speed > 0) ? max(speed, minPWM) : min(speed, -minPWM);
}

void drive(int leftFront, int leftBack, int rightFront, int rightBack) {
  // Per-wheel direction correction (MotorInvert[] in config_fw.hpp)
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

void motorsStop() {
  drive(0, 0, 0, 0);
}
