// pi_core/camera.hpp — V4L2 MJPG capture (raw JPEG, no decode inside the
// backend) + a dedicated capture thread that always drains the camera so old
// frames do not pile up in the buffer. Mirrors RawCamera / CaptureThread in
// rpi_elp_cam.py.
#pragma once

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace pi_core {

std::string fourcc_str(int value);

class RawCamera {
public:
    // Opens the device and forces the format; throws std::runtime_error on
    // failure (open error or no first frame) — caller decides how to report.
    RawCamera(const std::string& device, int width, int height, int fps);
    ~RawCamera();

    RawCamera(const RawCamera&) = delete;
    RawCamera& operator=(const RawCamera&) = delete;

    // Blocking read of one raw frame. ok=false on a failed grab.
    bool read(cv::Mat& out);

    void release();

    int actual_w() const { return actual_w_; }
    int actual_h() const { return actual_h_; }
    double actual_fps() const { return actual_fps_; }
    int actual_fourcc() const { return actual_fourcc_; }
    bool raw_mode() const { return raw_mode_; }  // true = 1-ch raw MJPG

private:
    cv::VideoCapture cap_;
    bool opened_ = false;
    int actual_w_ = 0, actual_h_ = 0;
    double actual_fps_ = 0.0;
    int actual_fourcc_ = 0;
    bool raw_mode_ = false;
};

class CaptureThread {
public:
    explicit CaptureThread(RawCamera& camera);
    ~CaptureThread();

    CaptureThread(const CaptureThread&) = delete;
    CaptureThread& operator=(const CaptureThread&) = delete;

    void start();
    void stop();               // signals and joins

    // Returns a new frame since `last_seq` (clone under lock) or ok=false.
    bool get_latest(long long last_seq, cv::Mat& out, long long& new_seq);
    double fps() const;        // frames / seconds since thread start
    long long errors() const;

private:
    void run();

    RawCamera& camera_;
    std::thread thread_;
    std::atomic<bool> stop_{false};

    std::chrono::steady_clock::time_point t0_{};

    mutable std::mutex mu_;
    cv::Mat latest_;
    long long seq_ = 0;
    long long frames_ = 0;
    long long errors_ = 0;
};

}  // namespace pi_core
