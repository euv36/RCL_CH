#include <Arduino.h>
#include "drive.hpp"
#include "ball.hpp"
#include "config.hpp"
#include "controller.hpp"
#include "hardware.hpp"
#include "gyro.hpp"
#include "gate.hpp"
#include "Line.hpp"


void advancedCatch() {
  // Serial.println(distance);
  while (ball.getDistance() > BallCircleRadius) {
    // driveAngle(getBallAngle() + getTangentAngle());
    // if (getBallDistance() <= HoldBallRadius) {
    //   holdBall();
    // // }
    // }
    // Serial.println(ball.getDistance());
    if (ball.isFound()) {
      ball.updateStatus();
      driveAngleWithRotation(ball.getAngle() + ball.getTangentAngle(), calculateSpeedPD(ball.getAngle()));
      // Serial.print("Ball angle = ");
      // Serial.println(ballAngle);
      // Serial.print("Tangent angle = ");
      // Serial.println(tangentAngle);
      // Serial.print("Drive angle = ");
      // Serial.println(angleToDrive);
      // Serial.print("Angular speed = ");
      // Serial.println(angularSpeed);
    } else {
      drive(0, 0, 0, 0);
    }
  }
  // holdBall();
  // driveAroundBall(200);
  // turnToBall();
}

void followBallWithRotation() {
  ball.updateStatus();
  driveAngleWithRotation(ball.getAngle(), calculateSpeedPD(ball.getAngle()));
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
  int angularSpeed = calculateSpeedPD(ball.getAngle());
  if (abs(angularSpeed) >= 40) {
    drive(angularSpeed, angularSpeed, -angularSpeed, -angularSpeed);
  } else {
    drive(0, 0, 0, 0);
  }
}

void debugAngularSpeed() {
  // Serial.print("Speed = ");
  ball.updateStatus();
  Serial.println(calculateSpeedPD(ball.getAngle()));
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
  static long long kickTime = millis();
  ball.updateStatus();
  if (ball.isFound()) {
    driveAngleWithRotation(ball.getAngle(), calculateSpeedPD(ball.getAngle()));
  } else {
    searchBall();
  }
  int lineAngle = getLineAngle();
  if (lineAngle != -1 ) {
    driveFromLine(lineAngle);
  }
  if (millis() - kickTime > 250) {
    kick();
    kickTime = millis();
  }
}

void driveAroundBall(int time) {
  long long startTime = millis();
  while (millis() - startTime < time) {
    ball.updateStatus();
    if (ball.isFound()) {
      driveAngleWithRotation(ball.getAngle() + ball.getTangentAngle(), calculateSpeedPD(ball.getAngle()));
    }
  }
}

#ifdef FORWARD

void play_() {
  int lineAngle = getLineAngle();
  while (!ball.isCatched()) {
    followBall();
    if (ball.getDistance() < 600) {
      holdBall();
    } else {
      releaseBall();
    }
  }
  long long startTime = millis();
  while (ball.isCatched() && millis() - startTime < 2000) {
    lineAngle = getLineAngle();
    alignGyro(GATEANGLE);
    // ddriveAngleWithRotation(GATEANGLE.rawValue(), calculateSpeedPD((getGyroAngle() - GATEANGLE).rawValue()));
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
    ball.updateStatus();
  }
  if (ball.isCatched()) {
    score();
  }
}

#endif

#ifdef GOALKEEPER

void play() {}

#endif

void goalkeeper() {
  long long lineTime = millis();
  ball.updateStatus();
  while (ball.getDistance() > HoldBallRadius && !ball.isCalm()) {
    // if (ball.getAngle() > -85) {
    //   driveAngle(180);
    // } else if (ball.getAngle() < -95) {
    //   driveAngle(0);
    // } else {
    //   drive(0, 0, 0, 0);
    // }
    // if (millis() - lineTime > 5000) {
    //   int lineAngle = getLineAngle();
    //   driveAngle(90);
    //   while (lineAngle == -1) {
    //     lineAngle = getLineAngle();
    //   }
    //   driveFromLine(lineAngle);
    // }
    ball.updateStatus();
  }
  long long startTime = millis();
  while (millis() - startTime < 1500) {
    followBall();
  }
  kick();
  turnToAngle(GATEANGLE, 35, 2.0, 10.0);
  // driveAngle(180);
  startTime = millis();
  while(millis() - startTime < 1000) {
    driveAngle(180);
    int lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
  }
}

void driveFromLine(int angle) {
  driveAngle(angle + 180);
  delay(400);
  while (getLineAngle() != -1);
}


void alignWithBallAndGate() {
  int lineAngle = getLineAngle();
  ball.updateStatus();
  while (abs(gateAngle - ball.getRawAngle()) > 10) {
    ball.updateStatus();
    if (ball.isFound()) {
       driveAngleWithRotation(ball.getAngle() + ball.getTangentAngle(), calculateSpeedPD(ball.getAngle()));
    } else {
      drive(0, 0, 0, 0);
    }
    lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
  }
  drive(0, 0, 0, 0);
  // while(1);
}

void searchBall() {
  drive(0, 0, 0, 0);
  if (ball.getAngle() > 0) {
    drive(40, 40, -40, -40);
  } else {
    drive(-40, -40, 40, 40);
  }
}

void alignWithBallAndAngle(int angle) {
  int lineAngle = getLineAngle();
  ball.updateStatus();
  while (abs(ball.getAngle()) > 10) {
    ball.updateStatus();
    if (ball.isFound()) {
       driveAngleWithRotation(ball.getAngle() + ball.getTangentAngle(), calculateSpeedPD(getGyroAngle().rawValue() - angle));
    } else {
      drive(0, 0, 0, 0);
    }
    lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
  }
  drive(0, 0, 0, 0);
  // while(1);
}

void showPlay() {
  while(1) {
    alignWithBallAndAngle(GATEANGLE.rawValue());
    driveAngle(0);
    delay(1500);

  }
}

void alignGyro(GyroAngle angle) {
  // GyroAngle startAngle = getGyroAngle();
  float kg = 1.0;
  GyroAngle currentAngle = getGyroAngle();
  driveAngle((currentAngle - angle).rawValue());
}

void advancedPlay() {
  static bool startFlag = false;
  long long startTime = millis(); 
  if (!startFlag) {
    turnToAngle(GATEANGLE, 40, 5.0, 10.0);
    startFlag = true;
  }
  ball.updateStatus();
  GyroAngle currentAngle = getGyroAngle();
  while (abs(ball.getAngle()) > 10) {
    if (ball.isFound()) {
      driveAngleWithRotation(ball.getAngle() + 35, calculateSpeedPD((GATEANGLE - currentAngle).rawValue()));
    } else {
      drive(0, 0, 0, 0);
    }
    int lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
    if (millis() - startTime > 3000) {
      turnToAngle(GATEANGLE, 35, 2.0, 10.0);
      startTime = millis();
      drive(0, 0, 0, 0);
    }
     ball.updateStatus();
     currentAngle = getGyroAngle();
  }
  // holdBall();
  static long long kickTime = millis();
  while (abs(ball.getAngle()) < 15/* && !ball.isCatched()*/) {
    // followBall();

    ball.updateStatus();
    currentAngle = getGyroAngle();
    int lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
    // if (millis() - startTime > 1500) {
    //   turnToAngle(GATEANGLE, 35, 2.0, 10.0);
    //   startTime = millis();
    //   drive(0, 0, 0, 0);
    // }
    if (millis() - kickTime > 750) {
      kick();
      kickTime = millis();
    }
    driveAngleWithRotation((currentAngle - GATEANGLE).rawValue(), calculateSpeedPD((GATEANGLE - currentAngle).rawValue()));
  }
  // releaseBall();
  // turnToAngle(GATEANGLE, 35, 2.0, 10.0);
  // driveAngle(0);
  // long long attackTime = millis();
  // while(ball.isCatched() && millis() - attackTime < 500) {
  //   driveAngle(0);
  //   int lineAngle = getLineAngle();
  //   if (lineAngle != -1) {
  //     driveFromLine(lineAngle);
  //   }
  // }
  // kick();
}

void gatePlay() {
  static bool startFlag = false;
  long long startTime = millis(); 
  if (!startFlag) {
    turnToAngle(GATEANGLE, 40, 5.0, 10.0);
    startFlag = true;
  }
  ball.updateStatus();
  GyroAngle currentAngle = getGyroAngle();
  while (abs(ball.getAngle()) > 10) {
    driveAngleWithRotation(ball.getAngle() + 35, calculateSpeedPD((GATEANGLE - currentAngle).rawValue()));
    int lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
    if (millis() - startTime > 3000) {
      turnToAngle(GATEANGLE, 35, 2.0, 10.0);
      startTime = millis();
      drive(0, 0, 0, 0);
    }
     ball.updateStatus();
     currentAngle = getGyroAngle();
  }
  holdBall();
  while (abs(ball.getAngle()) < 15/* && !ball.isCatched()*/) {
    // followBall();

    ball.updateStatus();
    currentAngle = getGyroAngle();
    int lineAngle = getLineAngle();
    if (lineAngle != -1) {
      driveFromLine(lineAngle);
    }
    // if (millis() - startTime > 1500) {
    //   turnToAngle(GATEANGLE, 35, 2.0, 10.0);
    //   startTime = millis();
    //   drive(0, 0, 0, 0);
    // }
    driveAngleWithRotation(getGateAngle(), calculateSpeedPD(getGateAngle()));
  }
  releaseBall();
  // turnToAngle(GATEANGLE, 35, 2.0, 10.0);
  // driveAngle(0);
  // long long attackTime = millis();
  // while(ball.isCatched() && millis() - attackTime < 500) {
  //   driveAngle(0);
  //   int lineAngle = getLineAngle();
  //   if (lineAngle != -1) {
  //     driveFromLine(lineAngle);
  //   }
  // }
  // kick();
}

void searchGates() {
  while(1) {
    int speed = calculateSpeedPD(getGateAngle());
    drive(speed, speed, -speed, -speed);
  }
}

void invalidGoalkeeper() {
  while(abs(ball.getAngle()) > 10) {
    searchBall();
    ball.updateStatus();
  }
  if (ball.getDistance() < 400) {
    kick();
  }
}