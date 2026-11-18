// pi_core/streamer.hpp — JPEG video PUB on tcp://*:5555 in its own thread.
// Mirrors Streamer in rpi_elp_cam.py: HWM 1, LINGER 0, fps limit, NOBLOCK
// send, {"stream":true|false} toggles sending.
//
// Future extension point (Idea 2): multipart [JPEG, status JSON] — add a
// second frame in send_frame(); the laptop accepts both formats. Reserved,
// not implemented in this task (Idea 2 owns the Python side first).
#pragma once

#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

namespace pi_core {

class Streamer {
public:
    Streamer(zmq::context_t& ctx, int bind_port, int fps_limit, int quality,
             bool enabled);
    ~Streamer();

    Streamer(const Streamer&) = delete;
    Streamer& operator=(const Streamer&) = delete;

    void start();
    void stop();

    // Hand the latest frame to the streaming thread (copy under lock).
    void publish(const cv::Mat& frame);

    // From the color-listener thread on {"stream":true|false}.
    void set_enabled(bool value);

    double sent_fps() const;  // total sent / seconds since start

private:
    void run();

    zmq::context_t& ctx_;
    int bind_port_;
    int fps_limit_;
    int quality_;
    std::atomic<bool> enabled_{false};

    std::mutex mu_;
    cv::Mat frame_;
    long long seq_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::condition_variable cv_;
    std::atomic<long long> sent_{0};
    std::chrono::steady_clock::time_point t0_;
};

}  // namespace pi_core
