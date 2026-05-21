#pragma once

const int LineSensNum = 14;

extern int maxCalibr[LineSensNum];
extern int minCalibr[LineSensNum];
const int lineSensorPos[] = {5, 13, 12, 11, 10, 9, 8, 7, 6, 0, 1, 2, 3, 4};
const int lineSensorsTH[] = {40, 40, 25, 30, 40, 45, 40, 25, 45, 40, 25, 40, 35, 40};
const int TH = 30;

void setMuxOut(int out);
void readSens();
void calibrate();
void screenSaver();
void printSens();