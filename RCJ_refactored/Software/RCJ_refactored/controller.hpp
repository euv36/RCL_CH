#pragma once


constexpr float Kpa = 0.5;
constexpr float Kda = 0.5;

void advancedCatch();
void seekBall();
void debugAngularSpeed();
void score();
void driveToGate();
void searchBall();
void followBall();
void driveAroundBall(int time);
int calculateSpeedPD(int error);
void driveFromLine(int angle);
void alignWithBallAndAngle(int angle);
void showPlay();
void play_();
void goalkeeper();
void alignGyro(GyroAngle angle);
void advancedPlay();
void searchGates();
void gatePlay();
void invalidGoalkeeper();
