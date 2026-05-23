#pragma once

// Motors
const int Motors[] = {3, 5, 7, 9, 11, 13, 8, 10};
const int MotorLF1 = Motors[5];
const int MotorLF2 = Motors[4];
const int MotorLB1 = Motors[6];
const int MotorLB2 = Motors[7];
const int MotorRB1 = Motors[2];
const int MotorRB2 = Motors[3];
const int MotorRF1 = Motors[0];
const int MotorRF2 = Motors[1];

// UI
const int Key1 = 37;
const int Key2 = 35;
const int EncA = 4;
const int EncB = 2;
const int EncKey = 6;

// Sensors
const int Mux[] = {38, 36, 34, 32};
const int MuxOut = A14;
const int Seeker = 0x09;

// Ball
const int Dribbler_pin = 12;
const int Kicker = 18;