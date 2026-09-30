#include <Arduino.h>
#include "motors.hpp"
#include "config.hpp"

int baseSpeed = 70;

constexpr float MotorsAngleSIN = sin(120 * 0.017453);

void drive(int leftFront, int leftBack, int rightFront, int rightBack) {
  // Limiting speed
  leftFront = constrain(leftFront, -255, 255);
  leftBack = constrain(leftBack, -255, 255);
  rightFront = constrain(rightFront, -255, 255);
  rightBack = constrain(rightBack, -255, 255);
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
  speed = constrain(speed, -255, 255);
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
