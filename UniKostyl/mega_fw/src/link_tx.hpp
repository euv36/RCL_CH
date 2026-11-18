#pragma once
#include <Arduino.h>

// link_tx — Arduino→Pi telemetry, FINAL binary framing
// (PROTOCOL.md §3/§4.2, approved 2026-11-18).
//
//   T_HDG  0x11,  8 B, on every fresh DMP packet (~100 Hz)
//   T_LINE 0x12, 38 B, every mux poll (50 Hz in the approved spec)
//   T_HB   0x13,  4 B, every 500 ms
//
// Frames are built here; the CRC routine is shared with the RX side
// (link_frame.hpp) so both directions are guaranteed bit-identical.

// Send heading: centidegrees (-18000..18000) + still flag. Called on every
// fresh DMP packet (~100 Hz native).
void send_heading(int heading_cdeg, bool still);

// Send 16 raw line values (ADC 0..1023) + the latest catcher bit.
// Called at the line poll rate.
void send_lines(const int values[16]);

// Record the catcher state; the bit rides in the next T_LINE frame
// (PROTOCOL.md §4.2 — no separate catcher frame).
void send_catcher(bool closed);

// Heartbeat.
void send_hb();
