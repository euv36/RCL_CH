#include <Arduino.h>
#include "debug.hpp"
#include "ball.hpp"
#include "line.hpp"
#include "gyro.hpp"
#include "motors.hpp"
#include "hardware.hpp"
#include "config.hpp"
#include "controller.hpp"
#include "gate.hpp"

void printLine() {
  line.updateStatus();
  int angle = rand() % 360 - 180;
  driveAngle(angle);
  int* rawLine = line.getRawValues();
  bool* binLine = line.getBinValues();
  Serial.println("/////////////////////////////////////////////////////////////////////////////////////////");
  Serial.print("First circle: ");
  for (int i = 0; i < LineSensNum; i += 2) {
    Serial.print(rawLine[i]);
    Serial.print(" ");
  }
  Serial.print(" | ");
  for (int i = 0; i < LineSensNum; i += 2) {
    Serial.print(binLine[i]);
    Serial.print(" ");
  }
  Serial.println((line.isFirstCircleLine()) ? "Slowing down!!!" : "No line in first circle");
  Serial.print("Second circle: ");
  for (int i = 1; i < LineSensNum; i += 2) {
    Serial.print(rawLine[i]);
    Serial.print(" ");
  }
  Serial.print(" | ");
  for (int i = 1; i < LineSensNum; i += 2) {
    Serial.print(binLine[i]);
    Serial.print(" ");
  }
  Serial.println((line.isSecondCircleLine()) ? "Going back!!!" : "No line in second circle");
  Serial.print("Angle = ");
  Serial.println(line.getAngle());
  delay(500);
}

void printBall() {
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

void printGyro() {
  GyroAngle angle;
  while (1) {
    angle = getGyroAngle();
    Serial.println(angle.rawValue());
    delay(500);
    // clear(Buffer);
  }
}

void rawTestDrive() {
  drive(100, 0, 0, 0);
  delay(2000);
  drive(0, 100, 0, 0);
  delay(2000);
  drive(0, 0, 100, 0);
  delay(2000);
  drive(0, 0, 0, 100);
  delay(2000);
}

void testKick() {
  while(!digitalRead(LeftKey));
  Serial.println("KICK!");
  kick();
}

void followBall() {
  while(1) {
    ball.updateStatus();
    driveAngle(ball.getAngle());
  }
}

void followBallWithRotation() {
  while (1) {
    ball.updateStatus();
    driveAngleWithRotation(ball.getAngle(), pd(ball.getAngle(), p, d));
  }
}

void debugBallDetector() {
  Serial.println(digitalRead(Ball_detector));
  delay(200);
}

void printGateAngle() {
  gate.updateStatus();
  Serial.print("Angle = ");
  Serial.println(gate.getAngle());
  Serial.print("Area = ");
  Serial.println(gate.getArea());
  delay(200);
}

void followGate() {
  gate.updateStatus();
  driveAngle(gate.getAngle());
}

void debugLine() {
  driveAngle(0);
  while(1) {
    checkLine();
  }
}