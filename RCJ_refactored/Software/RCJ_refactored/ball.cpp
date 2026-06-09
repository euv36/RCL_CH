#include <Arduino.h>
#include "ball.hpp"
#include "drive.hpp"
#include "config.hpp"

bool isBallFound = false;
bool isBallCatched = false;

long long calmTime = millis();


Locator locator;

int getBallAngle() {
  int angle = 0;
  static int lastAngle = angle;
  if (getBallStrength() > 80) {
    // Serial.println("Ball is close, switching reading modes!");
    angle = 360 - (5 * locator.readAngleClose());
  } else {
    angle = 360 - (5 * locator.readAngle());
  }
  if (angle == -915) {
    isBallFound = false;
    return lastAngle;
  } else {
    isBallFound = true;
  }
  if (abs(angle - lastAngle) > 10) {
    calmTime = millis();
  }
  if (angle > 180) {
    angle -= 360;
  }
  lastAngle = angle;
  return angle;
}

int getBallStrength() {
  int strength = locator.readStrength();
  static int lastStrength = strength;
  if (strength == 0) {
    isBallFound = false;
    return lastStrength;
  }
  else {
    isBallFound = true;
  }
  return strength;
}

int getBallDistance() {
  int strength = getBallStrength();
  // Serial.println(strength);
  return ((strength) ? ReferenceDistance / getBallStrength() : ReferenceDistance);
}

void debugLocator() {
  while (1) {
    Serial.print("Angle = ");
    Serial.println(getBallAngle());
    Serial.print("Strength = ");
    Serial.println(getBallStrength());
    delay(200);
  }
}

void checkBallCatched() {
  isBallCatched = !digitalRead(Dribbler_magnet);
}

void printIsBallCatched() {
  // while(1) {
    checkBallCatched();
    Serial.println(isBallCatched);
  // }
}