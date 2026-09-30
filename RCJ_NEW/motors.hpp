#pragma once

extern int baseSpeed;

const int BS = 70;
const int LS = 60;
const int CS = 70;

void drive(int leftFront, int leftBack, int rightFront, int rightBack);
void driveAngle(float angle, int bs = baseSpeed);
void driveAngleWithRotation(int angle, int angularSpeed, int bs = baseSpeed);

struct Motor {
  const int pinA, pinB;
  int currentSpeed;
  int targetSpeed;
  Motor(int a, int b) : pinA(a), pinB(b){}

  void drive(int speed);
  // void calculate
};