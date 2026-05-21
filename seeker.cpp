#include <Arduino.h>
#include "seeker.hpp"
#include "config.hpp"
#include "drive.hpp"

int ballPos[2];

void initWire() {
  Wire.begin();
  // Wire.beginTransmission(Seeker);
}

void readSeeker() {
  int res = Wire.requestFrom(Seeker, 4);
  Serial.println("Waiting for info...");
  Serial.print("Requested bytes = ");
  Serial.println(res);
  while(!Wire.available());
  byte b1 = Wire.read();
  byte b2 = Wire.read();
  ballPos[0] = ((uint16_t)b1 << 8) | b2;
  b1 = Wire.read();
  b2 = Wire.read();
  ballPos[1] = ((uint16_t)b1 << 8) | b2;
}

void showBallPos() {
  readSeeker();
  Serial.print("Angle = ");
  Serial.println(ballPos[0]);
  Serial.print("Distance = ");
  Serial.println(ballPos[1]);
}

// void followBall() {
//   while (digitalRead(Key1)) {
//     readSeeker();
//     driveAngle(ballPos[0]);
//   }
// }