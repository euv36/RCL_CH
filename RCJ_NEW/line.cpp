#include <Arduino.h>
#include "motors.hpp"
#include "config.hpp"
#include "line.hpp"
#include "controller.hpp"

Line line;

// const int lineSensorPos[LineSensNum]{};

bool lineValuesTH[LineSensNum]{};
int lineValues[LineSensNum]{};
int minCalibr[LineSensNum]{};
int maxCalibr[LineSensNum]{};
bool isLine = false;
int sensOnLine = 0;

void setMuxOut(int out) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(Mux[i], out & (1 << i));
  }
}


// Private methods
void Line::readSens() {
  resetTempVariables();
  sensOnLine = 0;
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    int a = analogRead(MuxOut);
    int b = analogRead(MuxOut);
    int c = analogRead(MuxOut);
    values[i] = medianFilter(a, b, c);
    binValues[i] = values[i] > TH;  //lineSensorsTH[i];
    // Serial.println(values[i] > lineSensorsTH[i]);
    if (binValues[i]) {
      sensOnLine++;
    }
    if (binValues[i]) {
      if (i % 2) {
        secondCircleFound = true;
      } else {
        firstCircleFound = true;
      }
    }
  }
  // if (sensOnLine >= 2) {
  //   found = true;
  // }
}

void Line::readSens_() {
  resetTempVariables();
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    values[i] = analogRead(MuxOut);
  }
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    values[i] = (values[i] + analogRead(MuxOut)) / 2;
    binValues[i] = values[i] > lineSensorsTH[i];//lineSensorsTH[i];
    if (binValues[i]) {
      if (i % 2) {
        secondCircleFound = true;
      } else {
        firstCircleFound = true;
      }
    }
  }
}

void Line::resetTempVariables() {
  firstCircleFound = false;
  secondCircleFound = false;
  sensOnLine = 0;
}

void Line::countAngle() {
  // Serial.println("Counting angle!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  int angleFlag = false;
  int weightSet1[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
  int weightSet2[] = { 0, 45, 90, 135, -180, -135, -90, -45 };
  int angle1 = 0, angle2 = 0;
  bool generalBinValues[LineSensNum / 2];
  for (int i = 0; i < LineSensNum; i += 2) {
    generalBinValues[i / 2] = binValues[i] || binValues[i + 1];
    if (generalBinValues[i / 2]) {
      angle1 += weightSet1[i / 2];
      angle2 += weightSet2[i / 2];
      sensOnLine++;
    }
  }
  if (sensOnLine == 0) {
    // Serial.println("No angle");
    angle = -1;
    return;
  }
  angle1 /= sensOnLine;
  angle2 /= sensOnLine;
  // Serial.print("Angle1 = ");
  // Serial.println(angle1);
  // Serial.print("Angle2 = ");
  // Serial.println(angle2);
  if (generalBinValues[0] || angle1 > 180) {
    angle = angle2;
  } else {
    angle = angle1;
  }
  
  Serial.print("Final angle = ");
  Serial.println(angle);
}

// Public methods
void Line::updateStatus() {
  readSens_();
  countAngle();
}

int Line::getAngle() {
  return angle;
}

int* Line::getRawValues() {
  return values;
}

bool* Line::getBinValues() {
  return binValues;
}

bool Line::isFirstCircleLine() {
  return firstCircleFound;
}

bool Line::isSecondCircleLine() {
  return secondCircleFound;
}

int Line::getSensOnLine() {
  return sensOnLine;
}
// void screenSaver() { To debug part
//   int angle = 0;
//   int lineAngle = -1;
//   while (1) {
//     driveAngle(angle);
//     while (lineAngle == -1) {
//       lineAngle = getLineAngle();
//     }
//     angle = 180 + lineAngle;
//     driveAngle(angle);
//     delay(200);
//     lineAngle = -1;
//   }
// }