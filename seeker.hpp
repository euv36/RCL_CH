#pragma once
#include <Wire.h>

extern int ballPos[2];

void initWire();
void readSeeker();
void showBallPos();
// void followBall();