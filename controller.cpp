#include <Arduino.h>
#include "drive.hpp"
#include "locator.hpp"
#include "config.hpp"
#include "controller.hpp"
#include "hardware.hpp"
#include "gyro.hpp"

int getBallDistance() {
  int strength = getBallStrength();
  // Serial.println(strength);
  return ((strength) ? ReferenceDistance / getBallStrength() : ReferenceDistance);
}

int getTangentAngle() {
  int currentDistance = getBallDistance();
  // Serial.print("Current distance = ");
  // Serial.println(currentDistance);
  // Serial.print("Asin = ");
  // Serial.println((double)asin(BallCircleRadius / (double)currentDistance));
  return (asin(BallCircleRadius / (double)currentDistance) / 0.017453);
}

void advancedCatch() {
  int distance = getBallDistance();
  // Serial.println(distance);
  while (distance > BallCircleRadius) {
    // driveAngle(getBallAngle() + getTangentAngle());
    // if (getBallDistance() <= HoldBallRadius) {
    //   holdBall();
    // // }
    // } 
  Serial.println(distance);
    if (distance != ReferenceDistance) {
      int ballAngle = getBallAngle();
      int tangentAngle = getTangentAngle();
      int angleToDrive = ballAngle + tangentAngle;
      int angularSpeed = countAngularSpeed();
      driveAngleWithRotation(angleToDrive, angularSpeed);
      Serial.print("Ball angle = ");
      Serial.println(ballAngle);
      Serial.print("Tangent angle = ");
      Serial.println(tangentAngle);
      // Serial.print("Drive angle = ");
      // Serial.println(angleToDrive);
      // Serial.print("Angular speed = ");
      // Serial.println(angularSpeed);
    } else {
      drive(0, 0, 0, 0);
    }
    // delay(200);
    distance = getBallDistance();
  }
  // holdBall();
  // driveAroundBall(200);
  // turnToBall();
}

void debugDistance() {
  Serial.print("Ball distance = ");
  Serial.println(getBallDistance());
}

void followBallWithRotation() {
  driveAngleWithRotation(getBallAngle(), countAngularSpeed());
}

void debugTangent() {
  Serial.print("Tangent = ");
  Serial.println(getTangentAngle());
  delay(200);
}

int countAngularSpeed() {
  static int lastError = 0;
  int error = getBallAngle() ;
  // Serial.print("Angle = ");
  // Serial.println(error);
  int delta = error * Kpa + (error - lastError) * Kda;
  lastError = error;
  return delta;
}

void seekBall() {
  int angularSpeed = countAngularSpeed();
  if (abs(angularSpeed) >= 40) {
    drive(angularSpeed, angularSpeed, -angularSpeed, -angularSpeed);
  } else {
    drive(0, 0, 0, 0);
  }
}

void debugAngularSpeed() {
  // Serial.print("Speed = ");
  Serial.println(countAngularSpeed());
}

void score() {
  while (!digitalRead(Key1));
  holdBall();
  delay(3000);
  turnByAngle(90, 40, 15.0, 2.5);
  kick();
}