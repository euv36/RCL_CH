// pi_core/streamer.cpp — see streamer.hpp
// UNVERIFIED ON HW: ZMQ PUB 5555 interop with the laptop SUB (cv_kostyl.py) —
// verify on the Pi per deploy.sh step 7/8.
#include "streamer.hpp"
#include "log.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstring>

namespace pi_core {

Streamer::Streamer(zmq::context_t& ctx, int bind_port, int fps_limit,
                   int quality, bool enabled)
    : ctx_(ctx),
      bind_port_(bind_port),
      fps_limit_(fps_limit < 1 ? 1 : fps_limit),
      quality_(quality),
      enabled_(enabled) {
    t0_ = std::chrono::steady_clock::now();
}

Streamer::~Streamer() { stop(); }

void Streamer::start() {
    thread_ = std::thread([this] { run(); });
}

void Streamer::stop() {
    stop_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Streamer::publish(const cv::Mat& frame) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        frame_ = frame.clone();  // main thread may reuse the source buffer
        ++seq_;
    }
    cv_.notify_all();
}

void Streamer::set_enabled(bool value) {
    const bool old = enabled_.exchange(value);
    if (old != value) {
        log_msg(std::string("Видеопоток: ") + (value ? "ВКЛ" : "ВЫКЛ"));
    }
}

double Streamer::sent_fps() const {
    const double dt = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - t0_)
                          .count();
    if (dt < 1e-6) return 0.0;
    return static_cast<double>(sent_.load(std::memory_order_relaxed)) / dt;
}

void Streamer::run() {
    zmq::socket_t sock(ctx_, zmq::socket_type::pub);
    sock.set(zmq::sockopt::sndhwm, 1);
    sock.set(zmq::sockopt::linger, 0);
    try {
        sock.bind("tcp://*:" + std::to_string(bind_port_));
        log_msg("Видео: PUB tcp://*:" + std::to_string(bind_port_));
    } catch (const zmq::error_t& e) {
        log_msg(std::string("Не удалось открыть видео-порт: ") + e.what(),
                "ERROR");
        return;
    }

    std::vector<uchar> jpeg;  // reused; capacity stabilizes after 1-2 frames
    jpeg.reserve(96 * 1024);

    long long last_seq = -1;
    auto last_send = std::chrono::steady_clock::time_point{};

    while (!stop_.load()) {
        // Wait for a new frame (no busy-wait; Python slept 2 ms per poll).
        cv::Mat frame;
        long long seq = -1;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return stop_.load() || seq_ != last_seq;
            });
            if (stop_.load()) break;
            seq = seq_;
            if (seq == last_seq || frame_.empty()) continue;
            frame = frame_;  // refcount copy under lock; frame_ stays valid
        }

        const auto now = std::chrono::steady_clock::now();
        // Disabled state consumes the seq so the loop never spins (Python
        // polled at 500 Hz with sleep(2 ms); deviation: after re-enable the
        // first frame goes out one frame-period later — no practical effect).
        if (!enabled_.load() || frame.empty()) {
            last_seq = seq;
            continue;
        }
        const double min_interval = 1.0 / fps_limit_;
        if (last_send.time_since_epoch().count() != 0) {
            const double dt =
                std::chrono::duration<double>(now - last_send).count();
            if (dt < min_interval) {
                // Drop early frames; net rate == fps_limit (Python retried
                // the same frame every 1 ms until the interval elapsed —
                // equivalent output rate, no 1 ms polling here).
                last_seq = seq;
                continue;
            }
        }

        try {
            jpeg.clear();
            const std::vector<int> params = {
                cv::IMWRITE_JPEG_QUALITY, quality_};
            const bool ok = cv::imencode(".jpg", frame, jpeg, params);
            if (ok) {
                try {
                    // NOBLOCK: a slow laptop must never block the streamer.
                    zmq::message_t msg(jpeg.data(), jpeg.size());
                    sock.send(msg, zmq::send_flags::dontwait);
                    sent_.fetch_add(1, std::memory_order_relaxed);
                } catch (const zmq::error_t&) {
                    // EAGAIN — subscriber not ready; frame dropped (same as
                    // Python zmq.Again).
                }
            }
        } catch (const cv::Exception& e) {
            log_debug(std::string("Ошибка отправки видео: ") + e.what());
        }

        last_seq = seq;
        last_send = now;
    }

    try {
        sock.close();
    } catch (...) {
    }
}

}  // namespace pi_core
