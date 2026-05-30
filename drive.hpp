#pragma once

const int BaseSpeed = 100;

void drive(int leftFront, int leftBack, int rightFront, int rightBack);
void driveAngle(float angle, int bs = BaseSpeed);
void testDrive();
void testDriveAngle();
void turnToBall();
void driveAroundBall(int time);
void driveAngleWithRotation(int angle, int angularSpeed, int bs = BaseSpeed);
void rawTestDrive();
// void advancedCatch();
