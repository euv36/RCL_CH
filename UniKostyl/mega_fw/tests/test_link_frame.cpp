// mega_fw/tests/test_link_frame.cpp — host-side tests for the binary
// Pi→Mega framing (PROTOCOL.md §3/§4.1).
//
// The firmware itself cannot run off-board, but the framing state machine and
// CRC are pure C++ (no Arduino.h), so they are tested here on the PC against
// golden wire bytes. Build/run on Windows (same ziglang tool as robot_core):
//
//   python -m ziglang c++ -O2 -std=c++17 -I UniKostyl/mega_fw/src \
//     UniKostyl/mega_fw/tests/test_link_frame.cpp \
//     -o UniKostyl/mega_fw/build/test_link_frame.exe
//   ./UniKostyl/mega_fw/build/test_link_frame.exe
//
// Exit code 0 = all checks green.

#include "link_frame.hpp"

#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("FAIL: %s\n", what);
    }
}

using link_frame::Frame;
using link_frame::Scanner;

// Golden T_DRIVE: seq=7, pwm=(70,-70,0,255), flags=0b11 (kicker+dribbler).
const uint8_t kDrive[] = {
    0xA5, 0x01, 0x07, 0x46, 0x00, 0xBA, 0xFF, 0x00,
    0x00, 0xFF, 0x00, 0x03, 0x2D,
};

// Golden T_STOP: seq=9.
const uint8_t kStop[] = {0xA5, 0x02, 0x09, 0x15};

// Golden T_DRIVE all-zero: seq=0.
const uint8_t kDrive0[] = {
    0xA5, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1F,
};

// Feed `n` bytes; collect every completed frame as a raw byte copy.
std::vector<std::vector<uint8_t>> feed(Scanner& scanner, const uint8_t* data,
                                       size_t n, Scanner::Result* last = nullptr) {
    std::vector<std::vector<uint8_t>> frames;
    Frame f;
    for (size_t i = 0; i < n; i++) {
        Scanner::Result r = scanner.push(data[i], f);
        if (last) *last = r;
        if (r == Scanner::Result::Frame) {
            frames.push_back(std::vector<uint8_t>(f.raw, f.raw + f.len));
        }
    }
    return frames;
}

void test_crc() {
    // Standard CRC-8/ATM check value (poly 0x07, init 0x00, no reflection).
    const uint8_t check_str[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    check(link_frame::crc8_atm(check_str, sizeof(check_str)) == 0xF4,
          "crc8 check value 0xF4");
}

void test_drive_golden() {
    Scanner s;
    std::vector<std::vector<uint8_t>> frames = feed(s, kDrive, sizeof(kDrive));
    check(frames.size() == 1, "drive: exactly one frame");
    if (frames.size() == 1) {
        check(frames[0] == std::vector<uint8_t>(kDrive, kDrive + sizeof(kDrive)),
              "drive: golden bytes round-trip through the scanner");
    }
    check(s.bad_crc() == 0, "drive: no CRC errors");
    check(s.unknown_type() == 0, "drive: no unknown types");
}

void test_stop_golden() {
    Scanner s;
    std::vector<std::vector<uint8_t>> frames = feed(s, kStop, sizeof(kStop));
    check(frames.size() == 1, "stop: exactly one frame");
    if (frames.size() == 1) {
        check(frames[0] == std::vector<uint8_t>(kStop, kStop + sizeof(kStop)),
              "stop: golden bytes");
    }
}

void test_two_frames_one_stream() {
    Scanner s;
    const uint8_t both[] = {
        0xA5, 0x01, 0x07, 0x46, 0x00, 0xBA, 0xFF, 0x00,
        0x00, 0xFF, 0x00, 0x03, 0x2D,
        0xA5, 0x02, 0x09, 0x15,
    };
    std::vector<std::vector<uint8_t>> frames = feed(s, both, sizeof(both));
    check(frames.size() == 2, "two frames back to back");
    if (frames.size() == 2) {
        check(frames[0][1] == 0x01 && frames[0][2] == 0x07, "first is T_DRIVE seq 7");
        check(frames[1][1] == 0x02 && frames[1][2] == 0x09, "second is T_STOP seq 9");
    }
}

void test_garbage_prefix_is_skipped() {
    Scanner s;
    const uint8_t junk[] = {'x', 'y', 0x00, 0xFF};
    feed(s, junk, sizeof(junk));
    std::vector<std::vector<uint8_t>> frames = feed(s, kDrive, sizeof(kDrive));
    check(frames.size() == 1, "garbage prefix: frame still found");
    check(s.bad_crc() == 0 && s.unknown_type() == 0,
          "garbage prefix: not counted as a frame error");
}

void test_bad_crc_dropped_and_resync() {
    Scanner s;
    uint8_t bad[sizeof(kDrive)];
    for (size_t i = 0; i < sizeof(kDrive); i++) bad[i] = kDrive[i];
    bad[sizeof(bad) - 1] ^= 0x01;  // flip one CRC bit
    Scanner::Result last = Scanner::Result::None;
    std::vector<std::vector<uint8_t>> frames = feed(s, bad, sizeof(bad), &last);
    check(frames.empty(), "bad CRC: frame rejected");
    check(last == Scanner::Result::BadCrc, "bad CRC: reported as BadCrc");
    check(s.bad_crc() == 1, "bad CRC: counted once");

    frames = feed(s, kStop, sizeof(kStop));
    check(frames.size() == 1, "bad CRC: next frame resyncs");
}

void test_unknown_type_dropped() {
    Scanner s;
    // T_HDG is telemetry: valid framing, wrong direction for the Mega scanner.
    const uint8_t hdg[] = {0xA5, 0x11, 0x03, 0xC7, 0xCF, 0x01, 0x00, 0x06};
    Scanner::Result last = Scanner::Result::None;
    std::vector<std::vector<uint8_t>> frames = feed(s, hdg, sizeof(hdg), &last);
    check(frames.empty(), "unknown type: ignored");
    check(s.unknown_type() >= 1, "unknown type: counted");
    (void)last;

    frames = feed(s, kDrive0, sizeof(kDrive0));
    check(frames.size() == 1, "unknown type: next frame resyncs");
}

void test_lost_byte_resync() {
    Scanner s;
    // Drop one payload byte from a drive frame (simulates a lost UART byte).
    uint8_t truncated[sizeof(kDrive) - 1];
    for (size_t i = 0; i < sizeof(truncated); i++) truncated[i] = kDrive[i];
    feed(s, truncated, sizeof(truncated));
    std::vector<std::vector<uint8_t>> frames = feed(s, kDrive0, sizeof(kDrive0));
    check(frames.size() == 1, "lost byte: next frame found after resync");
    if (frames.size() == 1) {
        check(frames[0][2] == 0x00, "lost byte: resynced frame is the new one (seq 0)");
    }
}

}  // namespace

int main() {
    test_crc();
    test_drive_golden();
    test_stop_golden();
    test_two_frames_one_stream();
    test_garbage_prefix_is_skipped();
    test_bad_crc_dropped_and_resync();
    test_unknown_type_dropped();
    test_lost_byte_resync();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
