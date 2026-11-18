#pragma once
#include "gyro.hpp"

// #define GOALKEEPER
#define FORWARD

// Motors
const int Motors[] = {3, 5, 7, 9, 11, 13, 8, 10};

// PWM limit: 255 = 100% duty (12V). 127 ≈ 50% duty (≈6V), 50 ≈ 20%.
// Every motor output is capped to this value. 10 is only ~4% (≈0.5V) - far
// below the point where most motors start moving.
const int MotorPWM = 70;

// Deadband compensation: minimum PWM applied to a wheel as soon as its
// command is non-zero. Order: left front, left back, right front, right back.
// Wheel крепление FIXED, so the +20 boost that was compensating slipping LB/RF
// is no longer needed and may now be biasing the pull - set to 0.
const int MotorMinPWM[4] = {0, 0, 0, 0};

// Per-wheel direction correction. Order: LF, LB, RF, RB.
// Set a wheel to 1 if it spins the wrong way. This is the safe way to fix a
// direction: do NOT swap MotorXX1/MotorXX2 below, because a half-swapped set
// makes the robot drive diagonally (the wheels no longer point the same way).
// Run rawTestWheels() (debug.cpp) to see which wheel is wrong.
// Measured with rawForward(): LF, LB and RB spin backwards, RF is correct.
const int MotorInvert[4] = {1, 1, 0, 1};

#ifdef FORWARD
const int MotorLF1 = Motors[6];
const int MotorLF2 = Motors[7];

const int MotorLB1 = Motors[5];
const int MotorLB2 = Motors[4];

const int MotorRB1 = Motors[0];
const int MotorRB2 = Motors[1];

const int MotorRF1 = Motors[3];
const int MotorRF2 = Motors[2];
#endif

#ifdef GOALKEEPER
// NOTE: keep the pin pairs consistent with each other. A single swapped pair
// inverts that wheel and makes the robot drive diagonally. Use MotorInvert[]
// above for direction fixes instead of swapping these.
const int MotorLF1 = Motors[5];
const int MotorLF2 = Motors[4];

const int MotorLB1 = Motors[7];
const int MotorLB2 = Motors[6];

const int MotorRB1 = Motors[1];
const int MotorRB2 = Motors[0];

const int MotorRF1 = Motors[3];
const int MotorRF2 = Motors[2];
#endif

// UI
const int RightKey = 37;
const int LeftKey = 35;
const int EncA = 4; 
const int EncB = 2;
const int EncKey = 6;

// Sensors
const int Mux[] = {38, 36, 34, 32};
const int MuxOut = A14;
// const int Seeker = 0x09;

// Ball
const int Dribbler_pin = 12;
const int Kicker = 18;
const int Ball_detector = 14;

extern GyroAngle GATEANGLE;