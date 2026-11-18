#pragma once
// link_frame.hpp — pure binary framing for the Pi→Mega direction
// (PROTOCOL.md §3/§4.1). NO Arduino.h: this header is host-testable, which is
// how the firmware's wire format is verified on the PC (tests/test_link_frame.cpp).
//
// Frame: SYNC(0xA5) | TYPE | SEQ | payload | CRC8
//   - little-endian, int16 two's complement;
//   - CRC-8/ATM (poly 0x07, init 0x00, no reflection) over TYPE..payload,
//     SYNC excluded;
//   - the scanner drops a bad-CRC or unknown-type frame whole and resyncs on
//     the next 0xA5 + known TYPE (fixed lengths => O(1) per candidate).
//
// The Mega only knows the inbound (Pi→Mega) types. Telemetry framing lives in
// link_tx.cpp.

#include <stddef.h>
#include <stdint.h>

namespace link_frame {

constexpr uint8_t kSync = 0xA5;
constexpr uint8_t kTypeDrive = 0x01;
constexpr uint8_t kTypeStop = 0x02;

// Longest inbound frame: T_DRIVE (13 bytes).
constexpr size_t kMaxFrame = 13;

// Length of a known Pi→Mega frame in bytes incl. SYNC and CRC; 0 = unknown.
inline size_t frame_len(uint8_t type) {
  if (type == kTypeDrive) return 13;
  if (type == kTypeStop) return 4;
  return 0;
}

// CRC-8/ATM over `n` bytes (poly 0x07, init 0x00). Must match the Pi side
// bit-for-bit (robot_core/src/link_codec.cpp uses the same routine).
inline uint8_t crc8_atm(const uint8_t* data, size_t n) {
  uint8_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// One complete, CRC-checked frame. `raw` points into the scanner's buffer and
// stays valid until the next push(); copy it if it must outlive that.
struct Frame {
  uint8_t type;
  uint8_t seq;
  uint8_t len;
  const uint8_t* raw;
};

// Byte-at-a-time receive state machine with a sliding resync window.
// Callers feed every received byte and act only on Result::Frame.
class Scanner {
 public:
  enum class Result { None, Frame, BadCrc, UnknownType };

  Result push(uint8_t b, Frame& out) {
    if (len_ == kMaxFrame) {
      drop_front(1);  // full buffer with no frame: bounded resync
    }
    buf_[len_++] = b;

    Result result = Result::None;
    for (;;) {
      if (len_ == 0) return result;
      if (buf_[0] != kSync) {
        size_t i = 1;
        while (i < len_ && buf_[i] != kSync) i++;
        drop_front(i);
        continue;
      }
      if (len_ < 2) return result;

      size_t need = frame_len(buf_[1]);
      if (need == 0) {
        unknown_type_++;
        result = Result::UnknownType;
        drop_front(1);  // the 0xA5 was a false/corrupt sync
        continue;
      }
      if (len_ < need) return result;

      if (crc8_atm(buf_ + 1, need - 2) != buf_[need - 1]) {
        bad_crc_++;
        result = Result::BadCrc;
        drop_front(1);
        continue;
      }

      out.type = buf_[1];
      out.seq = buf_[2];
      out.len = (uint8_t)need;
      out.raw = buf_;
      drop_front(need);
      return Result::Frame;
    }
  }

  uint32_t bad_crc() const { return bad_crc_; }
  uint32_t unknown_type() const { return unknown_type_; }

 private:
  void drop_front(size_t n) {
    if (n >= len_) {
      len_ = 0;
      return;
    }
    for (size_t i = n; i < len_; i++) buf_[i - n] = buf_[i];
    len_ -= n;
  }

  uint8_t buf_[kMaxFrame];
  size_t len_ = 0;
  uint32_t bad_crc_ = 0;
  uint32_t unknown_type_ = 0;
};

}  // namespace link_frame
