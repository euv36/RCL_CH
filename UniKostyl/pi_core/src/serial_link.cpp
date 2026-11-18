// pi_core/serial_link.cpp — see serial_link.hpp
// UNVERIFIED ON HW: /dev/serial0 termios open + Arduino auto-reset timing —
// verify with `pi_core --print-serial` (stdout path) and then the device path.
#include "serial_link.hpp"
#include "log.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <thread>

namespace pi_core {

namespace {

// Serial write budget, same as Python write_timeout=0.2.
constexpr double kWriteTimeoutS = 0.2;

class DeviceSink : public SerialSink {
public:
    DeviceSink(const std::string& port, int baud) : port_(port), baud_(baud) {}
    ~DeviceSink() override { close(); }

    bool open() {
        // O_NONBLOCK: we never want a wedged Arduino to stall the pipeline.
        fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) return false;

        struct termios tio;
        if (::tcgetattr(fd_, &tio) != 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        ::cfmakeraw(&tio);
        speed_t speed;
        switch (baud_) {
            case 9600:    speed = B9600; break;
            case 57600:   speed = B57600; break;
            case 115200:  speed = B115200; break;
            case 230400:  speed = B230400; break;
            default:      speed = B115200; break;
        }
        ::cfsetispeed(&tio, speed);
        ::cfsetospeed(&tio, speed);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~CSTOPB;
        tio.c_cflag &= ~PARENB;
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        if (::tcsetattr(fd_, TCSANOW, &tio) != 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        // Python sleeps 1.5 s after open (Arduino auto-reset on DTR), then
        // flushes both directions.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ::tcflush(fd_, TCIFLUSH);
        ::tcflush(fd_, TCOFLUSH);
        return true;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool write(const std::string& payload) override {
        if (fd_ < 0) return false;
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(kWriteTimeoutS));

        size_t off = 0;
        while (off < payload.size()) {
            const ssize_t n = ::write(fd_, payload.data() + off,
                                      payload.size() - off);
            if (n > 0) {
                off += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (std::chrono::steady_clock::now() >= deadline) break;
                struct pollfd pfd;
                pfd.fd = fd_;
                pfd.events = POLLOUT;
                const int pr = ::poll(&pfd, 1, 20);
                if (pr < 0 && errno != EINTR) break;
                continue;
            }
            break;  // hard error
        }
        if (off < payload.size()) {
            // Same degradation as Python: drop pending output, report failure.
            ::tcflush(fd_, TCOFLUSH);
            return false;
        }
        return true;
    }

    const char* name() const override { return port_.c_str(); }

private:
    std::string port_;
    int baud_;
    int fd_ = -1;
};

class StdoutSink : public SerialSink {
public:
    bool write(const std::string& payload) override {
        // Exact bytes to stdout, flushed immediately so a pipe consumer sees
        // lines in real time.
        const size_t n = std::fwrite(payload.data(), 1, payload.size(), stdout);
        if (n != payload.size()) return false;
        std::fflush(stdout);
        return true;
    }
    const char* name() const override { return "stdout(--print-serial)"; }
};

}  // namespace

std::unique_ptr<SerialSink> open_serial_device(const std::string& port,
                                               int baud) {
    auto sink = std::make_unique<DeviceSink>(port, baud);
    if (!sink->open()) return nullptr;
    log_msg("Serial открыт: " + port + " @ " + std::to_string(baud));
    return sink;
}

std::unique_ptr<SerialSink> open_stdout_sink() {
    return std::make_unique<StdoutSink>();
}

}  // namespace pi_core
