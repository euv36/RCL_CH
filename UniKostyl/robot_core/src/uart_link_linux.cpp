// uart_link_linux.cpp — termios plumbing for the Mega link.
//
// // UNVERIFIED ON HW: Linux termios + the real Mega have never met. Compile
// target: Pi only (see CMakeLists).

#include "uart_link_linux.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>

namespace robot_core {

UartLink::UartLink(const std::string& dev, bool print_tx) : dev_(dev), print_tx_(print_tx) {}

UartLink::~UartLink() {
    stop_rx();
    close_link();
}

bool UartLink::open_link() {
    if (print_tx_) {
        std::printf("[uart] print-tx smoke mode: no device opened\n");
        return true;
    }
    // UNVERIFIED ON HW: standard 115200 8N1 raw config for /dev/serial0.
    fd_ = ::open(dev_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::printf("[uart] cannot open %s\n", dev_.c_str());
        return false;
    }
    termios tio{};
    if (::tcgetattr(fd_, &tio) != 0) {
        std::printf("[uart] tcgetattr failed\n");
        return false;
    }
    std::memset(&tio, 0, sizeof(tio));
    ::cfmakeraw(&tio);
    ::cfsetispeed(&tio, B115200);
    ::cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(unsigned)CSTOPB;
    tio.c_cflag &= ~(unsigned)PARENB;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;  // 100 ms read timeout: the RX loop never spins hot
    if (::tcsetattr(fd_, TCSANOW, &tio) != 0) {
        std::printf("[uart] tcsetattr failed\n");
        return false;
    }
    ::tcflush(fd_, TCIOFLUSH);
    return true;
}

void UartLink::close_link() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void UartLink::start_rx() {
    if (rx_running_) return;
    rx_running_ = true;
    std::thread(&UartLink::rx_loop, this).detach();  // UNVERIFIED ON HW
}

void UartLink::stop_rx() {
    rx_running_ = false;
    // Detached thread observes the flag; no join (destructor-tolerant).
}

TelemetryState UartLink::telemetry() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return tel_;
}

void UartLink::handle_frames(const std::vector<RxFrame>& frames) {
    std::lock_guard<std::mutex> lock(mtx_);
    unsigned long now = (unsigned long)(monotonic_ms());
    for (const RxFrame& f : frames) {
        if (f.type == kTypeHeading) {
            tel_.heading = Heading(f.heading_cdeg / 100.0f);
            tel_.heading_still = f.still;
            tel_.heading_ms = now;
            tel_.have_heading = true;
        } else if (f.type == kTypeLines) {
            for (int i = 0; i < 16; i++) tel_.line_raw[i] = (int)f.line_raw[i];
            tel_.lines_ms = now;
            tel_.have_lines = true;
            // The catcher bit rides in T_LINE (PROTOCOL.md §4.2).
            tel_.catcher_closed = f.catcher_closed;
            tel_.catcher_ms = now;
            tel_.have_catcher = true;
        } else if (f.type == kTypeHeartbeat) {
            tel_.hb_ms = now;
            tel_.have_hb = true;
        } else {
            continue;  // TelemetryParser never emits other types
        }
        tel_.last_frame_ms = now;
        tel_.frames++;
    }
    // Cumulative diagnostics from the RX-thread-only scanner.
    tel_.bad_frames = rx_parser_.bad_frames();
}

unsigned long UartLink::monotonic_ms() {
    // UNVERIFIED ON HW: CLOCK_MONOTONIC on the Pi.
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

void UartLink::rx_loop() {
    char buf[256];
    while (rx_running_) {
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));  // no busy-wait
                continue;
            }
            std::printf("[uart] read error, link lost\n");
            return;  // control loop degrades via telemetry staleness
        }
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::vector<RxFrame> frames =
            rx_parser_.feed((const uint8_t*)buf, (size_t)n);
        handle_frames(frames);
    }
}

void UartLink::send_drive(const DriveBits& cmd) {
    std::string frame = format_drive_frame(tx_seq_++, cmd);
    tx_lines_++;
    if (print_tx_) {
        std::printf("TX T_DRIVE seq=%u pwm=%d,%d,%d,%d k=%d d=%d (13 B)\n",
                    (unsigned)frame[2], cmd.pwm[0], cmd.pwm[1], cmd.pwm[2],
                    cmd.pwm[3], cmd.kicker_fire ? 1 : 0, cmd.dribbler_on ? 1 : 0);
        return;
    }
    if (fd_ >= 0) {
        ssize_t n = ::write(fd_, frame.data(), frame.size());
        (void)n;  // short write -> telemetry staleness shows it; never fatal
    }
}

void UartLink::send_stop() {
    std::string frame = format_stop_frame(tx_seq_++);
    tx_lines_++;
    if (print_tx_) {
        std::printf("TX T_STOP seq=%u (4 B)\n", (unsigned)frame[2]);
        return;
    }
    if (fd_ >= 0) {
        ssize_t n = ::write(fd_, frame.data(), frame.size());
        (void)n;
    }
}

}  // namespace robot_core
