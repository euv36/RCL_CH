#pragma once
#include <Wire.h>

struct Locator {

  const int adr = 0x0E;

  bool init() {
    Wire.begin();
    Wire.beginTransmission(adr);
    Wire.write(0x00);
    return !Wire.endTransmission();
  }

  byte readAngle() {
    Wire.beginTransmission(adr);
    Wire.write(0x04);
    Wire.endTransmission();
    Wire.requestFrom(adr, 1);
    return Wire.read();
  }

  byte readStrength() {
    Wire.beginTransmission(adr);
    Wire.write(0x05);
    Wire.endTransmission();
    Wire.requestFrom(adr, 1);
    return Wire.read();
  }

  byte readAngleClose() {
    Wire.beginTransmission(adr);
    Wire.write(0x06);
    Wire.endTransmission();
    Wire.requestFrom(adr, 1);
    return Wire.read();
  }

  byte readStrengthClose() {
    Wire.beginTransmission(adr);
    Wire.write(0x07);
    Wire.endTransmission();
    Wire.requestFrom(adr, 1);
    return Wire.read();
  }
};

struct Line {

};