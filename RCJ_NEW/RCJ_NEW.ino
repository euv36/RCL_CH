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
  setupRPi();
  setupScreen();
  setupDribbler();
  setupGyro();
  Serial.print("Locator = ");
  Serial.println(locator.init());
  // printLine();
  while(!digitalRead(LeftKey));
}

void loop() {
  // digitalWrite(Kicker, HIGH);
  // delay(2000);
  // digitalWrite(Kicker, LOW);
  // delay(2000);
  // Serial.println("tmp msg");
  // printLine();
  // debugLine();
  // printGateAngle();
  // followGate();
  // printBall(); 
  // printGyro();
  // testKick();
  // rawTestDrive();
  // alignBallGate();
  // play();
  // modeSelect();
  // followBall();
  followBallWithRotation();
  // debugBallDetector();
}
