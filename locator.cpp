#include <Arduino.h>
#include "locator.hpp"
#include "drive.hpp"

constexpr float k = 1.0;

Locator locator;

int getBallAngle() {
  int angle;
  if (getBallStrength() > 80) {
    // Serial.println("Ball is close, switching reading modes!");
    angle = 360 - (5 * locator.readAngleClose());
  } else {
    angle = 360 - (5 * locator.readAngle());
  }
  return (angle > 180) ? angle - 360 : angle;
}

int getBallStrength() {
  return locator.readStrength();
}

void followBall() {
  driveAngle(getBallAngle());
}

void searchBall() {
  int angle;
  while (1) {
    angle = getBallAngle();
    while (angle > 10 || angle < -10) {
      if (angle > 0) {
        drive(35, 35, -35, -35);
      } else {
        drive(-35, -35, 35, 35);
      }
      angle = getBallAngle();
    }
    drive(0, 0, 0, 0);
  }
}


void debugLocator() {
  while(1) {
    Serial.print("Angle = ");
    Serial.println(getBallAngle());
    Serial.print("Distance = ");
    Serial.println(getBallStrength());
    delay(200);
  }
}