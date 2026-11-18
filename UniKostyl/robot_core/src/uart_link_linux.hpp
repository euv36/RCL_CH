#pragma once
// uart_link_linux.hpp — Mega telemetry RX + command TX over the Pi UART.
//
// // UNVERIFIED ON HW: this file uses Linux termios (/dev/serial0) and has
// only ever been compiled, never run against the real Mega. Bench it with the
// robot lifted first (see PROMPT_ROBOT §8 smoke list).
//
// ══ FINAL binary framing (PROTOCOL.md §3/§4, approved 2026-11-18) ══
// The wire grammar lives in link_codec.* (pure, tested); this file is only the
// termios plumbing around it.
//
// Threading: one RX thread (blocking read, byte-buffered, feeds the frame
// scanner, updates a mutex-guarded TelemetryState at frame rate) — the control
// thread never blocks on the UART. TX happens on the control thread at the tick
// rate. No busy-wait.

#include "link_codec.hpp"
#include "angles.hpp"
#include <mutex>
#include <string>
#include <vector>

namespace robot_core {

struct TelemetryState {
    // Gyro (from T_HDG frames, ~100 Hz).
    Heading heading;
    bool heading_still = false;
    unsigned long heading_ms = 0;
    bool have_heading = false;

    // Raw lines + catcher (from T_LINE frames, 50 Hz).
    int line_raw[16] = {0};
    unsigned long lines_ms = 0;
    bool have_lines = false;

    bool catcher_closed = false;
    unsigned long catcher_ms = 0;
    bool have_catcher = false;

    // Heartbeat (T_HB, 2 Hz).
    unsigned long hb_ms = 0;
    bool have_hb = false;

    unsigned long last_frame_ms = 0;   // any valid Mega frame
    unsigned long frames = 0;
    unsigned long bad_frames = 0;      // CRC / unknown / wrong-direction (diagnostics)
};

class UartLink {
public:
    // dev = "/dev/serial0". print_tx = --no-serial smoke mode: command frames
    // are summarized on stdout instead of written to the device.
    UartLink(const std::string& dev, bool print_tx);
    ~UartLink();

    bool open_link();
    void close_link();
    void start_rx();
    void stop_rx();

    // Snapshot of the latest telemetry (mutex-guarded copy).
    TelemetryState telemetry() const;

    // Send one T_DRIVE / T_STOP frame (binary, CRC8). Non-blocking-ish: a
    // short write failure is logged + counted, never fatal.
    void send_drive(const DriveBits& cmd);
    void send_stop();

    bool ok() const { return fd_ >= 0 || print_tx_; }
    unsigned long tx_lines() const { return tx_lines_; }

private:
    std::string dev_;
    bool print_tx_ = false;
    int fd_ = -1;
    bool rx_running_ = false;
    mutable std::mutex mtx_;
    TelemetryState tel_;
    unsigned long tx_lines_ = 0;

    uint8_t tx_seq_ = 0;   // per-direction rolling SEQ (diagnostics only)
    TelemetryParser rx_parser_;  // RX thread only

    void rx_loop();
    void handle_frames(const std::vector<RxFrame>& frames);
    static unsigned long monotonic_ms();  // CLOCK_MONOTONIC, ms  // UNVERIFIED ON HW
};

}  // namespace robot_core
