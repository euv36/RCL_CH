#pragma once

constexpr int BallCircleRadius = 75;
constexpr int ReferenceDistance = 10000;
constexpr float Kpa = 1;
constexpr float Kda = 2;

int getBallDistance();
int getTangentAngle();
void debugDistance();
void advancedCatch();
void debugTangent();
int countAngularSpeed();
void seekBall();
void debugAngularSpeed();
void score();