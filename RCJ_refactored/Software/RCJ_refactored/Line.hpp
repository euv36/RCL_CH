#pragma once

const int LineSensNum = 14;

extern int maxCalibr[LineSensNum];
extern int minCalibr[LineSensNum];
const int lineSensorPos[] = {10, 11, 12, 13, 0, 1, 2, 3, 4, 5, 6, 9, 8, 7};
const int lineSensorsTH[] = {40, 40, 30, 30, 40, 30, 25, 15, 30, 30, 25, 30, 35, 40};
const int TH = 30;

void setMuxOut(int out);
void readSens();
void calibrate();
void screenSaver();
void printSens();
int getLineAngle();
void printLineAngle();

struct Line {
  
};