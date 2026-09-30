#include <Arduino.h>
#include "ball.hpp"
#include "motors.hpp"
#include "config.hpp"

Ball ball;

Locator locator;

/// Private methods

void Ball::countAngle() {
  int tmpAngle = 0;
  static int lastAngle = tmpAngle;
  static int lastRawAngle = rawAngle;
  tmpAngle = 5 * locator.readAngle();
  rawAngle = tmpAngle;
  Serial.print("Tmp angle = ");
  Serial.println(tmpAngle);
  if (tmpAngle == 1275) {
    found = false;
    return;
  } else {
    found = true;
  }
  if (abs(rawAngle - lastRawAngle) > 10) {
    calmTime = millis();
  }
  if (tmpAngle > 180) {
    tmpAngle -= 360;
  }
  angle = tmpAngle;
  lastAngle = angle;
  lastRawAngle = rawAngle;
}

void Ball::countStrength() {
  int tmpStrength = locator.readStrength();
  static int lastStrength = tmpStrength;
  if (tmpStrength == 0) {
    found = false;
    return lastStrength;
  }
  else {
    found = true;
  }
  strength = tmpStrength;
}

void Ball::countDistance() {
  distance = ReferenceDistance / strength;
}

void Ball::countTangentAngle() {
  tangentAngle = (asin(BallCircleRadius / (double)distance) / 0.017453);
  if (angle > 0) {
    tangentAngle = -tangentAngle;
  }
}

void Ball::checkIfCatched() {
  if (!digitalRead(Ball_detector)) {
    catched = true; 
  } else {
    catched = false;
  }
}

/// Public methods
void Ball::updateStatus() {
  countStrength();
  countAngle();
  countTangentAngle();
  countDistance(); 
  checkIfCatched();
}

int Ball::getDistance() {
  return distance;
}

bool Ball::isCatched() {
  return catched;
}

bool Ball::isFound() {
  return found;
} 

int Ball::getAngle() {
  return angle;
}

int Ball::getRawAngle() {
  return rawAngle;
}

int Ball::getStrength() {
  return strength;
}

int Ball::getTangentAngle() {
  return tangentAngle;
}

bool Ball::isCalm () {
  return (millis() - calmTime > 5000);
}
