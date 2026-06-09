#include "config.hpp"
#include "drive.hpp"
#include "gyro.hpp"
#include "ball.hpp"
#include "Line.hpp"
#include "UI.hpp"
#include "hardware.hpp"
#include "drive.hpp"
#include "controller.hpp"
#include "gate.hpp"

void setup() {
  // Initialize pins
  Serial.begin(9600);
  for (int i = 0; i < 8; i++) {
    pinMode(Motors[i], OUTPUT);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(Mux[i], OUTPUT);
  }
  pinMode(Key1, INPUT_PULLUP);
  pinMode(Key2, INPUT_PULLUP);
  pinMode(EncA, INPUT_PULLUP);
  pinMode(EncB, INPUT_PULLUP);
  pinMode(EncKey, INPUT_PULLUP);
  pinMode(Dribbler_magnet, INPUT_PULLUP);
  // initWire();
  initKicker();
  // if (!locator.init()) {
  //   Serial.println("Locator isn't initilized!!!");
  // }
  // Serial.print("Oled is ");
  // Serial.println((displayInit()) ? "ready" : "NOT ready");
  // Serial.println("Initializing gyro...");
  // rawTestDrive();
  // initGyro();
  // Test motors
  initDribbler();
  locator.init();
  // setupRaspberry();
  // attachInterrupt(digitalPinToInterrupt(EncA), readEnc, CHANGE);
  // attachInterrupt(digitalPinToInterrupt(EncB), readEnc, CHANGE);
  // kick();
  Serial.println("Waiting for pressing a key");
  while(!digitalRead(Key1));
  // calibrate();
  // screenSaver();
  // printSens();
  // testDriveAngle();
  // rawTestDrive();

}

void loop() {
  // play();
  // Reading ball position
  // debugEnc();
  // testDrive();
  // Serial.println("//////Ball pos//////");
  // showBallPos();
  // delay(500);
  // Serial.print("Angle = ");
  // Serial.println(getGyroAngle().rawValue());
  printIsBallCatched();
  holdBall();
  // debugLocator();
  // searchBall();
  // followBall();
  // driveAroundBall(2000);
  // debugDistance();
  // debugTangent();
  // advancedCatch();
  // getGateAngle();
  // driveToGate();
  // printGyro();
  // testTurnAngle();
  // debugAngularSpeed();
  // releaseBall();
  // drive(0, 0, 0, 0);
  // while(1);
  // seekBall();
  // menuControl();
  // rawTestDrive();
  // testTurnAngle();
  // testAlignment();
  // testKick();
  // score();
  // printLineAngle();
  // rawTestDrive();
}
