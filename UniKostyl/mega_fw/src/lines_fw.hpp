#pragma once
#include <Arduino.h>

// lines_fw — raw polling of the 16 line sensors through the 4-channel mux,
// ported from rcj_arduino_src/line.cpp (readSens_ / setMuxOut / fastLineAdc).
//
// RAW VALUES ONLY: no thresholds, no binarization, no angle computation —
// per the target split, countAngle() and the weight tables moved to the Pi
// (robot_core/src/line_logic.*). The Mega streams raw ADC values.

const int LineSensNum = 16;

// Mux channel per sensor index (rcj line.hpp lineSensorPos, unchanged).
extern const int lineSensorPos[LineSensNum];

// Mux select write (PORTC/PORTD direct, verified against the Mega pinout).
void setMuxOut(int out);

// Reads all 16 sensors into values[16]: per sensor one discarded conversion
// after the mux switch (settle) + two settled samples averaged — the exact
// rcj readSens_() pass. Returns the number of microseconds the poll took
// (diagnostics for the >100 Hz loop budget).
unsigned long lines_poll(int values[LineSensNum]);
