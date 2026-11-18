#include <Arduino.h>
#include "link_tx.hpp"
#include "link_frame.hpp"
#include "config_fw.hpp"

// FINAL binary framing — PROTOCOL.md §3/§4.2 (approved 2026-11-18).
//
// All writes go to Serial2 (the Pi link). SEQ increments once per emitted
// frame (per direction); it is for drop diagnostics only. The largest frame is
// T_LINE at 38 bytes, which fits the 64-byte Serial2 TX buffer.
//
// Bandwidth (approved budget): 8 B @100 Hz + 38 B @50 Hz + 4 B @2 Hz ≈ 2.7 kB/s,
// ~24 % of 115200 — no blocking risk on the link.

namespace {

constexpr uint8_t kTypeHeading = 0x11;
constexpr uint8_t kTypeLines = 0x12;
constexpr uint8_t kTypeHeartbeat = 0x13;

HardwareSerial& link() { return Serial2; }

uint8_t txSeq = 0;
bool catcherClosed = false;

void send_frame(uint8_t type, const uint8_t* payload, size_t payloadLen) {
  uint8_t buf[40];
  buf[0] = link_frame::kSync;
  buf[1] = type;
  buf[2] = txSeq++;
  for (size_t i = 0; i < payloadLen; i++) {
    buf[3 + i] = payload[i];
  }
  buf[3 + payloadLen] = link_frame::crc8_atm(buf + 1, 2 + payloadLen);
  link().write(buf, 4 + payloadLen);

#if FW_DEBUG_TEXT
  Serial.print("tx ");
  Serial.print(type, HEX);
  Serial.print(" seq=");
  Serial.print(buf[2]);
  Serial.print(" len=");
  Serial.println(4 + payloadLen);
#endif
}

void put_i16(uint8_t* p, int v) {
  uint16_t u = (uint16_t)v;
  p[0] = (uint8_t)(u & 0xFF);
  p[1] = (uint8_t)(u >> 8);
}

}  // namespace

void send_heading(int heading_cdeg, bool still) {
  if (heading_cdeg > 18000) heading_cdeg = 18000;
  if (heading_cdeg < -18000) heading_cdeg = -18000;
  uint8_t p[4];
  put_i16(p, heading_cdeg);
  p[2] = still ? 0x01 : 0x00;
  p[3] = 0;  // reserved
  send_frame(kTypeHeading, p, sizeof(p));
}

void send_lines(const int values[16]) {
  uint8_t p[34];
  for (int i = 0; i < 16; i++) {
    int v = values[i];
    if (v < 0) v = 0;
    if (v > 1023) v = 1023;
    p[2 * i] = (uint8_t)(v & 0xFF);
    p[2 * i + 1] = (uint8_t)((v >> 8) & 0xFF);
  }
  p[32] = catcherClosed ? 0x01 : 0x00;  // FLAGS bit0
  p[33] = 0;                            // reserved
  send_frame(kTypeLines, p, sizeof(p));
}

void send_catcher(bool closed) {
  // No frame: the bit rides in T_LINE (PROTOCOL.md §4.2).
  catcherClosed = closed;
}

void send_hb() {
  send_frame(kTypeHeartbeat, nullptr, 0);
}
