#include <Arduino.h>
#include <EEPROM.h>
#include "Line.hpp"

const int CalibrAdr = 100;

void loadEEPROM() {
  int adr = CalibrAdr;
  for (int i = 0; i < LineSensNum; i++) {
    EEPROM.get(adr, minCalibr[i]);
    EEPROM.get(adr + sizeof(int), maxCalibr[i]);
    adr += 2 * sizeof(int);
  }
}

void saveEEPROM() {
  int adr = CalibrAdr;
  for (int i = 0; i < LineSensNum; i++) {
    EEPROM.put(adr, minCalibr[i]);
    EEPROM.put(adr = sizeof(int), maxCalibr[i]);
    adr += 2 * sizeof(int);
  }
}