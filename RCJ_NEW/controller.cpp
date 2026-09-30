#include <Arduino.h>
#include "controller.hpp"
#include "ball.hpp"
#include "gyro.hpp"
#include "motors.hpp"
#include "gate.hpp"
#include "line.hpp"
#include "hardware.hpp"

void alignBallGate() {
  baseSpeed = BS;
  int correctAngle = 60;
  ball.updateStatus();
  // GyroAngle currentAngle = getGyroAngle();
  if (ball.getAngle() < 0) {
    correctAngle = -60;
  }
  Serial.println("Aligning...");
  Serial.println(ball.getAngle());
  while (abs(ball.getAngle()) >= 5) {
    ball.updateStatus();
    gate.updateStatus();
    checkLine();
    // Serial.println("Changing speed!");
    // Serial.println(ball.isFound());
    if (ball.isFound()) {
      // Serial.println(ball.getAngle() + correctAngle);
      driveAngleWithRotation(ball.getAngle() + correctAngle, pd((gate.getAngle()), p, d));
    }
    // currentAngle = getGyroAngle();
  }
  // Serial.println("Aliogned!!!");
  // drive(0, 0, 0, 0);
  // while(1);
}

void catchBall() {
  // GyroAngle currentAngle = getGyroAngle();
  baseSpeed = LS;
  while (!ball.isCatched() && abs(ball.getAngle()) < 10) {
    ball.updateStatus();
    gate.updateStatus();
    checkLine();
    driveAngleWithRotation(ball.getAngle(), pd((gate.getAngle()), p, d));
    // currentAngle = getGyroAngle();
  }
}



void score() {
  gate.updateStatus();
  while (gate.getArea() < 2000 && ball.isCatched()) {
    ball.updateStatus();
    gate.updateStatus();
    driveAngleWithRotation(gate.getAngle(), pd(gate.getAngle(), p, d));
    checkLine();
  }
  kick();
}

void findGate() {}

int medianFilter(int a, int b, int c) {
  if ((a > b && a < c) || (a > c && a < b)) {
    return a;
  }
  if ((b > a && b < c) || (b > c && b < a)) {
    return b;
  }
  if ((c > a && c < b) || (c > b && c < a)) {
    return c;
  }
}

int pd(double error, float p, float d) {
  static int lastError = error;
  bool sign = error < 0;
  error = abs(error);
  int delta = error * p + (error - lastError) * d;
  lastError = error;
  if (sign) {
    return -delta;
  } else {
    return delta;
  }
}

void checkLine() {
  line.updateStatus();
  static int slowTime = millis();
  // if (line.getSensOnLine() >= 2) {
    if (line.isFirstCircleLine()) {
      driveFromLine();
    }
    if (line.isSecondCircleLine()) {
      // Serial.println("Going back!!!");
      driveFromLine();
      // Serial.print("Base speed = ");
      // Serial.println(baseSpeed);
    }
  // }
}

void driveFromLine() {
  // Serial.println(line.getAngle());
  // drive(0, 0, 0, 0);
  // while(1);
  driveAngle(line.getAngle() + 180);
  baseSpeed = BS;
  while (line.getAngle() != -1) {
    line.updateStatus();
    // driveAngle(180 - line.getAngle());
  }
}

void play() {
  alignBallGate();
  catchBall();
  score();
}