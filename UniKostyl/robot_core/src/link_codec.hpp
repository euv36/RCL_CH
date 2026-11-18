#pragma once
// link_codec.hpp — FINAL binary Pi↔Mega link framing (PROTOCOL.md §3/§4,
// approved 2026-11-18). Pure and testable; this is the ONLY framing-aware seam
// on the robot_core side (UartLink supplies the bytes, everything else sees
// decoded values).
//
// Frame: SYNC(0xA5) | TYPE | SEQ | payload | CRC8
//   - little-endian, int16 two's complement;
//   - CRC-8/ATM (poly 0x07, init 0x00, no reflection) over TYPE..payload,
//     SYNC excluded;
//   - fixed lengths per TYPE; a bad-CRC/unknown frame is dropped whole and the
//     scanner resyncs on the next 0xA5 + known TYPE.
//
// Pi → Mega: T_DRIVE 0x01 (13 B), T_STOP 0x02 (4 B).
// Mega → Pi: T_HDG 0x11 (8 B), T_LINE 0x12 (38 B), T_HB 0x13 (4 B).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace robot_core {

constexpr uint8_t kSyncByte = 0xA5;
constexpr uint8_t kTypeDrive = 0x01;
constexpr uint8_t kTypeStop = 0x02;
constexpr uint8_t kTypeHeading = 0x11;
constexpr uint8_t kTypeLines = 0x12;
constexpr uint8_t kTypeHeartbeat = 0x13;

// Longest frame in the catalog: T_LINE (38 bytes).
constexpr size_t kMaxFrameBytes = 38;

// CRC-8/ATM (poly 0x07, init 0x00). Must match mega_fw/src/link_frame.hpp.
uint8_t crc8_atm(const uint8_t* data, size_t n);

// Frame length incl. SYNC and CRC; 0 = unknown TYPE.
size_t frame_len_for_type(uint8_t type);

struct DriveBits {
    int pwm[4] = {0, 0, 0, 0};  // LF, LB, RF, RB (logical, pre-invert)
    bool kicker_fire = false;
    bool dribbler_on = false;
};

// Build one complete frame. PWM is clamped to ±255 before packing
// (PROTOCOL.md §6.1); reserved flag bits are zero.
std::string format_drive_frame(uint8_t seq, const DriveBits& cmd);
std::string format_stop_frame(uint8_t seq);

// Validate + decode a complete raw frame (starting at SYNC). Used by the
// conformance tests and the SimMega spec model; the live TX path only formats.
bool decode_drive_frame(const uint8_t* frame, size_t len, DriveBits& out);
bool decode_stop_frame(const uint8_t* frame, size_t len);

// One decoded Mega→Pi telemetry frame.
struct RxFrame {
    uint8_t type = 0;
    uint8_t seq = 0;
    int16_t heading_cdeg = 0;   // T_HDG
    bool still = false;         // T_HDG FLAGS bit0
    uint16_t line_raw[16] = {0};  // T_LINE
    bool catcher_closed = false;  // T_LINE FLAGS bit0
};

// Byte-at-a-time scanner with a sliding resync window (all five TYPEs known).
// `Frame::raw` points into the scanner buffer and stays valid until the next
// push; copy it if it must outlive that.
class FrameScanner {
public:
    struct Frame {
        uint8_t type;
        uint8_t seq;
        const uint8_t* raw;
        size_t len;
    };
    enum class Result { None, Frame, BadCrc, UnknownType };

    Result push(uint8_t b, Frame& out);

    uint32_t bad_crc() const { return bad_crc_; }
    uint32_t unknown_type() const { return unknown_type_; }

private:
    void drop_front(size_t n);

    uint8_t buf_[kMaxFrameBytes];
    size_t len_ = 0;
    uint32_t bad_crc_ = 0;
    uint32_t unknown_type_ = 0;
};

// Pi-side telemetry parser: scans a read buffer and returns every valid
// T_HDG/T_LINE/T_HB frame. Frames validly framed but addressed the other way
// (T_DRIVE/T_STOP) are counted as bad, never decoded as telemetry.
class TelemetryParser {
public:
    std::vector<RxFrame> feed(const uint8_t* data, size_t n);

    uint32_t bad_frames() const {
        return scanner_.bad_crc() + scanner_.unknown_type() + wrong_direction_;
    }

private:
    FrameScanner scanner_;
    uint32_t wrong_direction_ = 0;
};

}  // namespace robot_core
