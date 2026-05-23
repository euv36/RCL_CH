#pragma once

constexpr int HoldBallRadius = 70;
constexpr int BallCircleRadius = 30;
constexpr int ReferenceDistance = 10000;
constexpr float Kpa = 0.8;
constexpr float Kda = 1;

int getBallDistance();
int getTangentAngle();
void debugDistance();
void advancedCatch();
void debugTangent();
int countAngularSpeed();
void seekBall();
void debugAngularSpeed();
void score();