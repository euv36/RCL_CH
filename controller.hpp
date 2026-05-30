#pragma once

constexpr int HoldBallRadius = 100;
constexpr int BallCircleRadius = 60;
constexpr int ReferenceDistance = 10000;
constexpr float Kpa = 0.5;
constexpr float Kda = 0.5;

int getBallDistance();
int getTangentAngle();
void debugDistance();
void advancedCatch();
void debugTangent();
int countAngularSpeed();
void seekBall();
void debugAngularSpeed();
void score();
void driveToGate();