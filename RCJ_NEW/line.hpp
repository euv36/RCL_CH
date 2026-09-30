#pragma once
#include "config.hpp"

const int LineSensNum = 16;

extern int maxCalibr[LineSensNum];
extern int minCalibr[LineSensNum];
 const int lineSensorPos[] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};
// const int lineSensorPos[] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};
// const int lineSensorsTH[] = {  ,   ,   ,   ,  ,  ,  ,  ,  ,  ,  ,  ,  ,  ,   ,   };
#ifdef GOALKEEPER
const int lineSensorsTH[] = {600, 1000, 650, 650, 350, 700, 650, 300, 700, 700, 700, 700, 600, 600, 900, 250};
// extern const int lineSensorPos[] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};
#endif
#ifdef FORWARD
const int lineSensorsTH[] = {240, 250, 250, 280, 300, 300, 900, 280, 250, 250, 250, 250, 300, 300, 250, 300};
// extern const int lineSensorPos[] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};
#endif
const int TH = 950;

void setMuxOut(int out);
void readSens();
void calibrate();
void screenSaver();
int getLineAngle();
void printLineAngle();

struct Line {
private:
  int values[LineSensNum];
  bool binValues[LineSensNum];
  int angle;
  bool firstCircleFound = false, secondCircleFound = false;
  int sensOnLine;
  // int lineAngle = 0;
  void countAngle();
  void readSens();
  void resetTempVariables();
  void readSens_();
public:
  int getAngle();
  void updateStatus();
  int* getRawValues();
  bool* getBinValues();
  bool isFirstCircleLine();
  bool isSecondCircleLine();
  int getSensOnLine();
};

extern Line line;