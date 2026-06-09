#include <Arduino.h>
#include "drive.hpp"
#include "ball.hpp"
#include "config.hpp"
#include "controller.hpp"
#include "hardware.hpp"
#include "gyro.hpp"
#include "gate.hpp"
#include "Line.hpp"

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
    if (isBallFound) {
      int ballAngle = getBallAngle();
      int tangentAngle = getTangentAngle();
      int angleToDrive = ballAngle + tangentAngle;
      int angularSpeed = calculateSpeedPD(ballAngle);
      driveAngleWithRotation(angleToDrive, angularSpeed);
      Serial.print("Ball angle = ");
      Serial.println(ballAngle);
      Serial.print("Tangent angle = ");
      Serial.println(tangentAngle);
      Serial.print("Drive angle = ");
      Serial.println(angleToDrive);
      Serial.print("Angular speed = ");
      Serial.println(angularSpeed);
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
  int error = getBallAngle();
  bool sign = error < 0;
  error = abs(error);
  // Serial.print("Angle = ");
  // Serial.println(error);
  int delta = error * Kpa + (error - lastError) * Kda;
  lastError = error;
  if (sign) {
    return -delta;
  } else {
    return delta;
  }
}

int calculateSpeedPD (int error) {
  static int lastError = 0;
  bool sign = error < 0;
  error = abs(error);
  int delta = error * Kpa + (error - lastError) * Kda;
  lastError = error;
  if (sign) {
    return -delta;
  } else {
    return delta;
  }
}

void seekBall() {
  int angularSpeed = calculateSpeedPD(getBallAngle());
  if (abs(angularSpeed) >= 40) {
    drive(angularSpeed, angularSpeed, -angularSpeed, -angularSpeed);
  } else {
    drive(0, 0, 0, 0);
  }
}

void debugAngularSpeed() {
  // Serial.print("Speed = ");
  Serial.println(calculateSpeedPD(getBallAngle()));
}

void score() {
  while (!digitalRead(Key1))
    ;
  holdBall();
  delay(3000);
  turnByAngle(90, 40, 15.0, 2.5);
  kick();
}

void driveToGate() {
  while (gateAngle > 20) {
    drive(35, 35, -35, -35);
  }
  drive(0, 0, 0, 0);
}

void followBall() {
  driveAngleWithRotation(getBallAngle(), calculateSpeedPD(getBallAngle()));
  int lineAngle = getLineAngle();
  if (lineAngle != -1 ) {
    driveFromLine(lineAngle);
  }
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

void driveAroundBall(int time) {
  long long startTime = millis();
  int sign = 1;
  int angle = getBallAngle();
  if (angle > 0) {
    sign = -1;
  }
  while (millis() - startTime <= time) {
    angle = getBallAngle();
    driveAngle(angle + 45 * sign);
  }
}

void play() {
  int lineAngle, ballAngle, gateAngle;
  while (!isBallCatched) {
    if (isBallFound) {
      lineAngle = getLineAngle();
      ballAngle = getBallAngle();
      driveAngleWithRotation(ballAngle, calculateSpeedPD(ballAngle));
      if (lineAngle != -1) {
        driveFromLine(lineAngle);
      }
      checkBallCatched();
    }
  }
  long long startTime = millis();
  while (isBallCatched && millis() - startTime < 3000) {
    lineAngle = getLineAngle();
    gateAngle = getGateAngle() + 180;
    driveAngleWithRotation(gateAngle, calculateSpeedPD(gateAngle));
    checkBallCatched();
  }
  if (isBallCatched) {
    score();
  }
}

void goalkeeper() {
  int distance = getBallDistance();
  int ballAngle = getBallAngle();
  long long lineTime = millis();
  while (distance > 100) {
    if (ballAngle > 90) {
      driveAngle(180);
    } else {
      driveAngle(0);
    }
    if (millis() - lineTime > 5000) {
      int lineAngle = getLineAngle();
      driveAngle(90);
      while (lineAngle == -1) {
        lineAngle = getLineAngle();
      }
      driveFromLine(lineAngle);
    }
    distance = getBallDistance();
    ballAngle = getBallAngle();
  }
  
}

void driveFromLine(int angle) {
  driveAngle(angle + 180);
  delay(400);
  while (getLineAngle() != -1);
}


void alignWithBallAndGate() {
  int ballAngle = getBallAngle();
  int tangentAngle = getTangentAngle();
  int lineAngle = getLineAngle();
  while (gateAngle - ballAngle > 10) {
    driveAngleWithRotation(ballAngle + tangentAngle, calculateSpeedPD(ballAngle));
    ballAngle = getBallAngle();
    tangentAngle = getTangentAngle();
    lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
  }
  drive(0, 0, 0, 0);
  // while(1);
}