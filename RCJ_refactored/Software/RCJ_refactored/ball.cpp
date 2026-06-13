#include <Arduino.h>
#include "ball.hpp"
#include "drive.hpp"
#include "config.hpp"

Ball ball;

Locator locator;

/// Private methods

void Ball::countAngle() {
  int tmpAngle = 0;
  static int lastAngle = tmpAngle;
  static int lastRawAngle = rawAngle;
  if (strength > 80) {
    // Serial.println("Ball is close, switching reading modes!");
    tmpAngle = 360 - (5 * locator.readAngleClose());
  } else {
    tmpAngle = 360 - (5 * locator.readAngle());
  }
  rawAngle = tmpAngle;
  if (tmpAngle == -915) {
    found = false;
    return lastAngle;
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
  if (!digitalRead(Dribbler_magnet)) {
    catched = true; 
  } else if (abs(angle) > 10 || distance > 500) {
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

/// Debug functions

void printInfo() {
  while (1) {
    ball.updateStatus();
    Serial.print("Angle = ");
    Serial.println(ball.getAngle());
    Serial.print("Strength = ");
    Serial.println(ball.getStrength());
    Serial.print("Distance = ");
    Serial.println(ball.getDistance());
    Serial.print("Tangent angle = ");
    Serial.println(ball.getTangentAngle());
    Serial.print("Found = ");
    Serial.println((ball.isFound()) ? "found" : "NOT FOUND!");
    Serial.print("Catched = ");
    Serial.println((ball.isCatched()) ? "CATCHED!" : "not catched");
    Serial.print("Calm = ");
    Serial.println((ball.isCalm()) ? "CALM!\n" : "not calm\n");
    delay(600);
  }
}