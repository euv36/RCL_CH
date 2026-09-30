#include <Arduino.h>
#include "gate.hpp"
#include "hardware.hpp"

Gate gate;

void Gate::countAngle() {
  if (angle < 0 || angle > 360) {
    angle = lastAngle;
    return;
  }
  angle -= 90;
  if (angle > 180) {
    angle -= 360;
  }
  lastAngle = angle;
}

void Gate::countArea() {
}

int Gate::getAngle() {
  return angle;
}

int Gate::getArea() {
  return area;
}

void Gate::updateStatus() {
  lastAngle = angle;
  if (piSerial.available()) {
    String input = piSerial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      if (input != "HB") {
        int commaIdx = input.indexOf(',');
        if (commaIdx >= 0) {
          angle = input.substring(0, commaIdx).toInt();
          area = input.substring(commaIdx + 1).toInt();
          countAngle();
        } else {
          angle = input.toInt();
        }
      }
    } else {
      while (piSerial.available()) piSerial.read();
    }
  }
}