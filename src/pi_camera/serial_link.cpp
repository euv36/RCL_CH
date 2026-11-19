#include "pi_camera/serial_link.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace camera_cpp {
namespace {

constexpr int kAngleChangeThreshold = 3;
constexpr double kMinSendIntervalSeconds = 0.10;

// Приводит угол к целому числу в диапазоне [0, 360), как в legacy.
int normalized_angle(float angle_deg) {
    int angle = static_cast<int>(std::lround(static_cast<double>(angle_deg)));
    angle %= 360;
    if (angle < 0) angle += 360;
    return angle;
}

// Разница углов с учётом перехода через 0/360: 359 -> 1 всего 2 градуса.
int wrapped_angle_difference(int a, int b) {
    int difference = std::abs(a - b) % 360;
    if (difference > 180) difference = 360 - difference;
    return difference;
}

std::string gate_line(int gate_id, int angle, float area_px) {
    const double clamped = std::clamp(static_cast<double>(area_px), 0.0, 1.0e9);
    return "G" + std::to_string(gate_id) + "," + std::to_string(angle) + ","
         + std::to_string(static_cast<int>(clamped)) + "\n";
}

} // namespace

SerialCommand update_gate_serial(GateSerialState& state, int gate_id, bool found,
                                 float angle_deg, float area_px, double now_seconds) {
    SerialCommand command;
    if (!found) {
        if (state.last_sent_found) {
            command.send = true;
            command.line = "G" + std::to_string(gate_id) + ",-1,0\n";
            state.last_sent_found = false;
            state.last_sent_angle = -1;
        }
        return command;
    }

    const int angle = normalized_angle(angle_deg);
    const bool first = !state.last_sent_found;
    const bool changed = wrapped_angle_difference(angle, state.last_sent_angle) >= kAngleChangeThreshold;
    const bool interval_ok = now_seconds - state.last_send_time >= kMinSendIntervalSeconds;
    if (!first && !(changed && interval_ok)) return command;

    command.send = true;
    command.line = gate_line(gate_id, angle, area_px);
    state.last_sent_found = true;
    state.last_sent_angle = angle;
    state.last_send_time = now_seconds;
    return command;
}

bool serial_port_available(const std::filesystem::path& port) {
    if (port.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(port, error) && !error;
}

SerialLink::SerialLink(std::string port, bool enabled)
    : port_(std::move(port)), enabled_(enabled) {
    if (!enabled_ || !serial_port_available(port_)) return;
#if !defined(_WIN32)
    fd_ = ::open(port_.c_str(), O_WRONLY | O_NOCTTY);
    if (fd_ < 0) return;
    struct termios options {};
    if (::tcgetattr(fd_, &options) == 0) {
        ::cfsetispeed(&options, B115200);
        ::cfsetospeed(&options, B115200);
        options.c_cflag |= CS8 | CLOCAL | CREAD;
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;
        ::tcsetattr(fd_, TCSANOW, &options);
    }
    ::tcflush(fd_, TCIOFLUSH);
#endif
}

SerialLink::~SerialLink() {
    close();
}

bool SerialLink::connected() const noexcept {
    return fd_ >= 0;
}

bool SerialLink::write(std::string_view line) noexcept {
    if (fd_ < 0 || line.empty()) return false;
#if !defined(_WIN32)
    const ssize_t written = ::write(fd_, line.data(), line.size());
    if (written != static_cast<ssize_t>(line.size())) return false;
    ::tcdrain(fd_);
    return true;
#else
    return false;
#endif
}

void SerialLink::close() noexcept {
    if (fd_ < 0) return;
#if !defined(_WIN32)
    ::close(fd_);
#endif
    fd_ = -1;
}

} // namespace camera_cpp
