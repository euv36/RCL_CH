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

extern Locator locator;


constexpr int ReferenceDistance = 10000;
constexpr int HoldBallRadius = 500;
constexpr int BallCircleRadius = 300;



struct Ball {
private:
  int angle = 0, rawAngle = 0, tangentAngle = 0, strength = 0, distance = ReferenceDistance;
  bool found = false, catched = false;
  long long calmTime;
  void countAngle();
  void countStrength();
  void countDistance();
  void countTangentAngle();
  void checkIfCatched();

public:
  int getAngle();
  int getStrength();
  int getDistance();
  int getRawAngle();
  int getTangentAngle();
  bool isCatched();
  bool isFound();
  bool isCalm();
  void updateStatus();
};

extern Ball ball;

void printInfo();