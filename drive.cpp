#include <Arduino.h>
#include "config.hpp"
#include "drive.hpp"
#include "locator.hpp"

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
  float k1 = sin((30 - angle) * 0.017453) / MotorsAngleSIN;
  float k2 = sin((30 + angle) * 0.017453) / MotorsAngleSIN;
  // Send
  drive(bs * k2, bs * k1, bs * k1, bs * k2);
}

void turnToBall() {
  int angle = getBallAngle();
  while (angle > 5 || angle < -5) {
    if (angle > 0) {
      drive(35, 35, -35, -35);
    } else {
      drive(-35, -35, 35, 35);
    }
    angle = getBallAngle();
  }
  drive(0, 0, 0, 0);
}

void driveAroundBall(int time) {
  long long startTime = millis();
  int sign = 1;
  int angle = getBallAngle();
  if (angle > 0) {
    sign = -1;
  }
  while(millis() - startTime <= time) {
    angle = getBallAngle();
    driveAngle(angle + 45 * sign);
  }
}

void testDrive() {
  while (1) {
    for (int i = 50; i <= 150; i++) {
      drive(i, i, i, i);
      delay(25);
    }
    for (int i = 150; i >= -150; i--) {
      drive(i, i, i, i);
      delay(25);
    }
    for (int i = -150; i <= 50; i++) {
      drive(i, i, i, i);
      delay(25);
    }
    drive(0, 0, 0, 0);
    delay(1000);
  }
}

void testDriveAngle() {
  while (1) {
    driveAngle(0);
    delay(1500);
    drive(0, 0, 0, 0);
    delay(1000);
    driveAngle(90);
    delay(1500);
    drive(0, 0, 0, 0);
    delay(1000);
    driveAngle(180);
    delay(1500);
    drive(0, 0, 0, 0);
    delay(1000);
    driveAngle(-90);
    delay(1500);
    drive(0, 0, 0, 0);
    delay(1000);
  }
}

void rawTestDrive() {
  // while(1) {
    drive(80, 0, 0, 0);
    delay(1000);
    drive(0, 0, 0, 0);
    Serial.println("Left front motor tested");
    delay(1000);
    drive(0, 80, 0, 0);
    delay(1000);
    drive(0, 0, 0, 0);
    Serial.println("Left back motor tested");
    delay(1000);
    drive(0, 0, 80, 0);
    delay(1000);
    drive(0, 0, 0, 0);
    Serial.println("Right front motor tested");
    delay(1000);
    drive(0, 0, 0, 80);
    delay(1000);
    drive(0, 0, 0, 0);
    Serial.println("Right back motor tested");
    delay(1000);
  // }
}

void driveAngleWithRotation(int angle, int angularSpeed, int bs) {
  float k1 = sin((30 - angle) * 0.017453) / MotorsAngleSIN;
  float k2 = sin((30 + angle) * 0.017453) / MotorsAngleSIN;
  drive(bs * k2 + angularSpeed, bs * k1 + angularSpeed, bs * k1 - angularSpeed, bs * k2 - angularSpeed);
}

// void advancedCatch() {
//   while(getBallStrength() < 80) {
//     followBall();
//   }
//   driveAroundBall(1000);
//   turnToBall();
//   // turnToBall();
// }