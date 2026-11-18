#pragma once
#include <Arduino.h>

// link_rx — Pi→Arduino command reception, FINAL binary framing
// (PROTOCOL.md §3/§4.1, approved 2026-11-18).
//
// The wire state machine + CRC live in link_frame.hpp (pure C++, host-tested
// by tests/test_link_frame.cpp). This file only maps decoded frames onto the
// executors and keeps the drive/watchdog semantics.
//
//   T_DRIVE 0x01 (13 B): 4 × int16 PWM (LF,LB,RF,RB) + FLAGS bit0 kicker,
//                        bit1 dribbler — resets the watchdog
//   T_STOP  0x02 (4 B): explicit stop — resets the watchdog
//
// A frame with bad CRC / unknown type is dropped whole; the scanner resyncs on
// the next SYNC+known TYPE. No partial application.

struct DriveCommand {
  bool valid;        // a complete, well-formed command was applied
  bool is_stop;      // STOP command (wheels 0 + kicker/dribbler released)
  int pwm[4];        // LF, LB, RF, RB, -255..255, pre-invert (logical)
  bool kicker_fire;  // kicker request bit
  bool dribbler_on;  // dribbler bit
};

// Feed one received byte. Returns true when a complete valid command frame was
// applied (the watchdog is fed inside).
bool parse_pi_byte(uint8_t b);
