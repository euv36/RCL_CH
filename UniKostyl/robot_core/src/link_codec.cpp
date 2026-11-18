#include "link_codec.hpp"

namespace robot_core {

namespace {

constexpr size_t kDriveLen = 13;
constexpr size_t kStopLen = 4;
constexpr size_t kHdgLen = 8;
constexpr size_t kLineLen = 38;
constexpr size_t kHbLen = 4;

int clamp_pwm(int v) {
    if (v < -255) return -255;
    if (v > 255) return 255;
    return v;
}

void put_i16(uint8_t* p, int v) {
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)(u & 0xFF);
    p[1] = (uint8_t)(u >> 8);
}

int16_t get_i16(const uint8_t* p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

}  // namespace

uint8_t crc8_atm(const uint8_t* data, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

size_t frame_len_for_type(uint8_t type) {
    switch (type) {
        case kTypeDrive: return kDriveLen;
        case kTypeStop: return kStopLen;
        case kTypeHeading: return kHdgLen;
        case kTypeLines: return kLineLen;
        case kTypeHeartbeat: return kHbLen;
        default: return 0;
    }
}

std::string format_drive_frame(uint8_t seq, const DriveBits& cmd) {
    uint8_t buf[kDriveLen];
    buf[0] = kSyncByte;
    buf[1] = kTypeDrive;
    buf[2] = seq;
    for (int i = 0; i < 4; i++) {
        put_i16(buf + 3 + 2 * i, clamp_pwm(cmd.pwm[i]));
    }
    buf[11] = (uint8_t)((cmd.kicker_fire ? 0x01 : 0x00) |
                        (cmd.dribbler_on ? 0x02 : 0x00));
    buf[12] = crc8_atm(buf + 1, kDriveLen - 2);
    return std::string((const char*)buf, kDriveLen);
}

std::string format_stop_frame(uint8_t seq) {
    uint8_t buf[kStopLen];
    buf[0] = kSyncByte;
    buf[1] = kTypeStop;
    buf[2] = seq;
    buf[3] = crc8_atm(buf + 1, kStopLen - 2);
    return std::string((const char*)buf, kStopLen);
}

bool decode_drive_frame(const uint8_t* frame, size_t len, DriveBits& out) {
    if (len != kDriveLen || frame[0] != kSyncByte || frame[1] != kTypeDrive) {
        return false;
    }
    if (crc8_atm(frame + 1, kDriveLen - 2) != frame[kDriveLen - 1]) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        out.pwm[i] = get_i16(frame + 3 + 2 * i);
    }
    out.kicker_fire = (frame[11] & 0x01) != 0;
    out.dribbler_on = (frame[11] & 0x02) != 0;
    return true;
}

bool decode_stop_frame(const uint8_t* frame, size_t len) {
    if (len != kStopLen || frame[0] != kSyncByte || frame[1] != kTypeStop) {
        return false;
    }
    return crc8_atm(frame + 1, kStopLen - 2) == frame[kStopLen - 1];
}

void FrameScanner::drop_front(size_t n) {
    if (n >= len_) {
        len_ = 0;
        return;
    }
    for (size_t i = n; i < len_; i++) buf_[i - n] = buf_[i];
    len_ -= n;
}

FrameScanner::Result FrameScanner::push(uint8_t b, Frame& out) {
    if (len_ == kMaxFrameBytes) {
        drop_front(1);  // full buffer with no frame: bounded resync
    }
    buf_[len_++] = b;

    Result result = Result::None;
    for (;;) {
        if (len_ == 0) return result;
        if (buf_[0] != kSyncByte) {
            size_t i = 1;
            while (i < len_ && buf_[i] != kSyncByte) i++;
            drop_front(i);
            continue;
        }
        if (len_ < 2) return result;

        size_t need = frame_len_for_type(buf_[1]);
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
        out.len = need;
        out.raw = buf_;
        drop_front(need);
        return Result::Frame;
    }
}

std::vector<RxFrame> TelemetryParser::feed(const uint8_t* data, size_t n) {
    std::vector<RxFrame> frames;
    if (data == nullptr) return frames;

    FrameScanner::Frame f;
    for (size_t i = 0; i < n; i++) {
        if (scanner_.push(data[i], f) != FrameScanner::Result::Frame) {
            continue;
        }
        RxFrame rx;
        rx.type = f.type;
        rx.seq = f.seq;
        if (f.type == kTypeHeading) {
            rx.heading_cdeg = get_i16(f.raw + 3);
            rx.still = (f.raw[5] & 0x01) != 0;
        } else if (f.type == kTypeLines) {
            for (int k = 0; k < 16; k++) {
                rx.line_raw[k] = (uint16_t)f.raw[3 + 2 * k] |
                                 ((uint16_t)f.raw[4 + 2 * k] << 8);
            }
            rx.catcher_closed = (f.raw[35] & 0x01) != 0;
        } else if (f.type != kTypeHeartbeat) {
            // T_DRIVE/T_STOP on the RX line: valid framing, wrong direction.
            wrong_direction_++;
            continue;
        }
        frames.push_back(rx);
    }
    return frames;
}

}  // namespace robot_core
