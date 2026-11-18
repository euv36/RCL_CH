#include "config.hpp"
#include "UI.hpp"
#include "hardware.hpp"
#include "debug.hpp"
#include "ball.hpp"
#include "controller.hpp"

void setup() {
  setupGPIO();
  setupSerial();
  setupWire();
#ifdef FORWARD
  setupRPi();
#endif
  setupScreen();
  setupDribbler();
  if (setupGyro()) {
    calibrateGyro();
  }
  Serial.print("Locator = ");
  Serial.println(locator.init());
  // printLine();
  baseAngle = getGyroAngle();
  while (!digitalRead(LeftKey))
    ;
}

void loop() {
  // digitalWrite(Kicker, HIGH);
  // delay(2000);
  // digitalWrite(Kicker, LOW);
  // delay(2000);
  // Serial.println("tmp msg");
  // printLine();
  debugLine();
  // printLineSensors();
  // printGateAngle();
  // followGate();
  // printBall();
  //  printGyro();
  // testKick();
  // rawTestDrive();
  // rawTestWheels();
  // rawForward();
  // driveForward(3000);
  // delay(1000);
  // Four-direction demo:  
  // driveForward(1000);  delay(500);
  // driveLeft(1000);     delay(500);
  // driveBackward(1000); delay(500);
  // driveRight(1000);    delay(500);
  // alignBallGate();
  // play();
  // Serial.println("Playing");
  // modeSelect();
  // // followBall();
  followBallWithRotation();
  // debugBallDetector();
  // headingSignTest();
}
