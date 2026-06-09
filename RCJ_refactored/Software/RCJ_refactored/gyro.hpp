#pragma once

class GyroAngle {
  // degree (-180 : 180)
  float rawAngle = 0.0f;
public:
  GyroAngle() = default;
  GyroAngle(float angle)
    : rawAngle(angle) {
  }

  float value() const {
    return (rawAngle < 0) ? rawAngle + 360.00f : rawAngle;
  }

  float rawValue() const {
    return rawAngle;
  }

  //operator float() { return (rawAngle < 0) ? rawAngle + 360.00f : rawAngle; }

  float operator()() {
    return (rawAngle < 0) ? rawAngle + 360.00f : rawAngle;
  }

  bool operator==(GyroAngle other) {
    return (rawAngle == other.rawAngle);
  }

  bool operator!=(GyroAngle other) {
    return !(*this == other);
  }

  // lhs & rhs (-180.00 : 180.00) degree
  // result (-180.00 : 180.00) degree
  GyroAngle& operator+=(const GyroAngle other) {
    rawAngle += other.rawAngle;
    if (rawAngle > 180) rawAngle -= 360;
    else if (rawAngle < -180) rawAngle += 360;
    return *this;
  }

  // lhs & rhs (-180.00 : 180.00) degree
  // result (-180.00 : 180.00) degree
  GyroAngle& operator-=(const GyroAngle other) {
    rawAngle -= other.rawAngle;
    if (rawAngle > 180) rawAngle -= 360.00;
    else if (rawAngle < -180) rawAngle += 360.00;
    return *this;
  }
};

inline GyroAngle operator-(GyroAngle lhs, const GyroAngle rhs) {
  return lhs -= rhs;
}

inline GyroAngle& operator+(GyroAngle lhs, const GyroAngle rhs) {
  return lhs += rhs;
}

extern GyroAngle startAngle;

void turnByAngle(int angle, int speed, float correction, float gap);
void turnToAngle(GyroAngle goalAngle, int speed, float gap, float correction);
bool initGyro();
void printGyro();
GyroAngle& getGyroAngle();
void testTurnAngle();
void testAlignment();

enum Turn_direction : byte {
  CW = 0,
  CCW
};