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

// Optimization (same behaviour as the original digitalWrite() loop):
// Mux[0]=38=PD7 (out bit0), Mux[1]=36=PC1 (bit1),
// Mux[2]=34=PC3 (bit2), Mux[3]=32=PC5 (bit3)
// (see variants/mega/pins_arduino.h).
void setMuxOut(int out) {
  uint8_t c = (uint8_t)((out & 0x02) | ((out & 0x04) << 1) | ((out & 0x08) << 2));
  PORTC = (PORTC & ~0x2A) | c;                       // PC1, PC3, PC5
  PORTD = (PORTD & ~0x80) | ((uint8_t)(out & 0x01) << 7); // PD7
}

// Fast ADC read for channel A14 = PK6 = ADC14 on the Mega 2560
// (see variants/mega/pins_arduino.h). Channels 8..15 require MUX5=1 in ADCSRB
// with ADMUX selecting bits 4..0 = 14 & 0x07 = 6. Identical to analogRead(A14)
// (same AVcc reference) but skips the pin->channel lookup. The ADC runs at
// 500 kHz (prescaler set in setupGPIO() in hardware.cpp).
static inline int fastLineAdc() {
  ADCSRB = (ADCSRB & ~(1 << MUX5)) | (1 << MUX5); // select channels 8..15
  ADMUX = (1 << REFS0) | 0x06;                     // AVcc reference, channel 6
  ADCSRA |= (1 << ADSC);                           // start conversion
  while (ADCSRA & (1 << ADSC)) {                   // wait for completion
  }
  return ADC;
}

// Private methods
void Line::readSens() {
  resetTempVariables();
  sensOnLine = 0;
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    int a = fastLineAdc();
    int b = fastLineAdc();
    int c = fastLineAdc();
    values[i] = medianFilter(a, b, c);
    binValues[i] = values[i] > TH;
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
}

// Optimization: the same averaging as the original two-pass code (first scan
// read once per sensor, second scan read again and averaged into values[i]).
// Merged into one pass with back-to-back consecutive samples of the same
// sensor - identical math, half the mux switches. The first conversion right
// after a mux switch is discarded: the mux output needs time to settle, and
// sampling immediately after the switch read the previous channel's charge.
void Line::readSens_() {
  resetTempVariables();
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    fastLineAdc();               // discard - let the line settle
    int a = fastLineAdc();       // settled sample
    int b = fastLineAdc();       // settled sample
    values[i] = (a + b) >> 1;
    binValues[i] = values[i] > lineSensorsTH[i];
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
  static const int weightSet1[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
  static const int weightSet2[] = { 0, 45, 90, 135, -180, -135, -90, -45 };
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
    angle = -1;
    return;
  }
  angle1 /= sensOnLine;
  angle2 /= sensOnLine;
  if (generalBinValues[0] || angle1 > 180) {
    angle = angle2;
  } else {
    angle = angle1;
  }
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
