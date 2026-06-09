#pragma once

constexpr int HoldBallRadius = 100;
constexpr int BallCircleRadius = 60;
constexpr float Kpa = 0.5;
constexpr float Kda = 0.5;

int getTangentAngle();
void debugDistance();
void advancedCatch();
void debugTangent();
int countAngularSpeed();
void seekBall();
void debugAngularSpeed();
void score();
void driveToGate();
void searchBall();
void followBall();
void driveAroundBall(int time);
int calculateSpeedPD(int error);
void driveFromLine(int angle);

#define GOALKEEPER
