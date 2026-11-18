#include <Arduino.h>
#include "link_rx.hpp"
#include "link_frame.hpp"
#include "motors_fw.hpp"
#include "executors_fw.hpp"
#include "watchdog_fw.hpp"
#include "config_fw.hpp"

// FINAL binary framing — PROTOCOL.md §3/§4.1 (approved 2026-11-18).
// The state machine and CRC are pure and host-tested (tests/test_link_frame.cpp).

static link_frame::Scanner scanner;

// Little-endian int16, two's complement (AVR-native order per the protocol).
static int16_t read_i16(const uint8_t* p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void apply_command(const DriveCommand& cmd) {
  if (cmd.is_stop) {
    motorsStop();
    kicker_release();
    dribbler_set(false);
  } else {
    drive(cmd.pwm[0], cmd.pwm[1], cmd.pwm[2], cmd.pwm[3]);
    kicker_request(cmd.kicker_fire);
    dribbler_set(cmd.dribbler_on);
  }
  watchdog_feed();  // any valid command resets the Mega watchdog

#if FW_DEBUG_TEXT
  Serial.print("cmd: ");
  Serial.print(cmd.is_stop ? "STOP" : "D");
  for (int i = 0; i < 4; i++) {
    Serial.print(' ');
    Serial.print(cmd.pwm[i]);
  }
  Serial.print(" k=");
  Serial.print(cmd.kicker_fire ? 1 : 0);
  Serial.print(" d=");
  Serial.println(cmd.dribbler_on ? 1 : 0);
#endif
}

bool parse_pi_byte(uint8_t b) {
  link_frame::Frame frame;
  link_frame::Scanner::Result r = scanner.push(b, frame);

#if FW_DEBUG_TEXT
  if (r == link_frame::Scanner::Result::BadCrc) {
    Serial.println("link_rx: dropped frame (bad CRC)");
  } else if (r == link_frame::Scanner::Result::UnknownType) {
    Serial.println("link_rx: dropped frame (unknown type)");
  }
#endif

  if (r != link_frame::Scanner::Result::Frame) {
    return false;
  }

  DriveCommand cmd;
  cmd.valid = true;
  cmd.kicker_fire = false;
  cmd.dribbler_on = false;
  if (frame.type == link_frame::kTypeStop) {
    cmd.is_stop = true;
    cmd.pwm[0] = cmd.pwm[1] = cmd.pwm[2] = cmd.pwm[3] = 0;
  } else {
    cmd.is_stop = false;
    for (int i = 0; i < 4; i++) {
      cmd.pwm[i] = read_i16(frame.raw + 3 + 2 * i);
    }
    uint8_t flags = frame.raw[11];
    cmd.kicker_fire = (flags & 0x01) != 0;
    cmd.dribbler_on = (flags & 0x02) != 0;
  }

  apply_command(cmd);
  return true;
}
