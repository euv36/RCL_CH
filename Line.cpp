#include <Arduino.h>
#include "drive.hpp"
#include "config.hpp"
#include "Line.hpp"

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

void readSens() {
  isLine = false;
  sensOnLine = 0;
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    lineValues[i] = analogRead(MuxOut);
  }
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    lineValues[i] = (lineValues[i] + analogRead(MuxOut)) / 2;
    lineValuesTH[i] = lineValues[i] > TH; //lineSensorsTH[i];
    if (lineValuesTH[i]) {
      isLine = true;
      sensOnLine++;
    }
  }
}


void calibrate() {
  drive(60, 60, -60, -60);
  int minSens[LineSensNum];
  int maxSens[LineSensNum];
  for (int i = 0; i < LineSensNum; i++) {
    minCalibr[i] = 1023;
    maxCalibr[i] = 0;
  }
  for (int i = 0; i < 2000; i++) {
    for (int j = 0; j < 10; j++) {
      readSens();
      for (int k = 0; k < LineSensNum; k++) {
        if ((j == 0) || lineValues[k] > maxSens[k]) {
          maxSens[k] = lineValues[k];
        }
        if ((j == 0) || lineValues[k] < minSens[k]) {
          minSens[k] = lineValues[k];
        }
      }
    }
    for (int j = 0; j < LineSensNum; j++) {
      if (minSens[j] > maxCalibr[j]) {
        maxCalibr[j] = minSens[j];
      }
      if (maxSens[j] < minCalibr[j]) {
        minCalibr[i] = maxSens[i];
      }
    }
  }
  drive(0, 0, 0, 0);
  Serial.println("MAX");
  for (int i = 0; i < LineSensNum; i++) {
    Serial.print(maxCalibr[i]);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println("MIN");
  for (int i = 0; i < LineSensNum; i++) {
    Serial.print(minCalibr[i]);
    Serial.print(" ");
  }
  Serial.println();
}

void printSens() {
  // int angle = rand() % 360 - 180;
  while (1) {
    // driveAngle(angle);
    readSens();
    for (int i = 0; i < LineSensNum; i++) {
      Serial.print(lineValues[i]);
      Serial.print(" ");
    }
    Serial.print("| ");
    for (int i = 0; i < LineSensNum; i++) {
      Serial.print(lineValuesTH[i]);
      Serial.print(" ");
    }
    delay(200);
    Serial.print("isLine = ");
    Serial.print((isLine) ? "ONLINE!!!!!!!!!!!!" : "not");
    Serial.println();
    // angle = rand() % 360 - 180;
  }
}

void screenSaver() {
  int angle = 0;
  while (1) {
    driveAngle(angle);
    readSens();
    while (isLine == false || sensOnLine < 3) {
      readSens();
    };
    angle = ((angle == 0) ? 180 : 0);
    driveAngle(angle);
    delay(1000);
    readSens();
  }
}