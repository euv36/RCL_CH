#include <Arduino.h>
#include "lines_fw.hpp"
#include "config_fw.hpp"

// Ported from rcj_arduino_src/line.cpp (READ-ONLY reference). The unused
// legacy paths (readSens() median-3, TH, binValues) are dropped: the split
// streams raw values only.

const int lineSensorPos[LineSensNum] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};

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
// 500 kHz (prescaler set in setupGPIO() in main.cpp).
static inline int fastLineAdc() {
  ADCSRB = (ADCSRB & ~(1 << MUX5)) | (1 << MUX5); // select channels 8..15
  ADMUX = (1 << REFS0) | 0x06;                     // AVcc reference, channel 6
  ADCSRA |= (1 << ADSC);                           // start conversion
  while (ADCSRA & (1 << ADSC)) {                   // wait for completion
  }
  return ADC;
}

unsigned long lines_poll(int values[LineSensNum]) {
  unsigned long t0 = micros();
  for (int i = 0; i < LineSensNum; i++) {
    setMuxOut(lineSensorPos[i]);
    fastLineAdc();               // discard - let the line settle
    int a = fastLineAdc();       // settled sample
    int b = fastLineAdc();       // settled sample
    values[i] = (a + b) >> 1;
  }
  return micros() - t0;
}
