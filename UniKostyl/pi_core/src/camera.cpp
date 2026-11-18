// pi_core/camera.cpp — see camera.hpp
// UNVERIFIED ON HW: V4L2 backend behaviour (MJPG FOURCC report, raw 1xN JPEG
// buffer with CAP_PROP_CONVERT_RGB=0, BUFFERSIZE=1) compiles only on the Pi —
// no OpenCV in the local Windows toolchain. Verify with `pi_core --probe`.
#include "camera.hpp"
#include "log.hpp"

#include <opencv2/videoio.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace pi_core {

std::string fourcc_str(int value) {
    char s[5];
    for (int i = 0; i < 4; ++i)
        s[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    s[4] = '\0';
    return std::string(s);
}

RawCamera::RawCamera(const std::string& device, int width, int height,
                     int fps) {
    // Linux: V4L2+MJPG (Windows would use MSMF; this code targets the Pi).
    if (!cap_.open(device, cv::CAP_V4L2)) {
        throw std::runtime_error(
            "Не удалось открыть " + device +
            ". Проверьте, что камера не занята другой программой (fuser " +
            device + ") и есть права на устройство.");
    }
    opened_ = true;
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap_.set(cv::CAP_PROP_FPS, fps);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
    // 0 = do not decode MJPG inside OpenCV; read() returns raw JPEG bytes.
    cap_.set(cv::CAP_PROP_CONVERT_RGB, 0);

    // The first read() forces the UVC format negotiation.
    cv::Mat first;
    if (!cap_.read(first) || first.empty()) {
        cap_.release();
        opened_ = false;
        throw std::runtime_error("Камера открылась, но не отдала ни одного кадра");
    }

    actual_w_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    actual_h_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    actual_fps_ = cap_.get(cv::CAP_PROP_FPS);
    actual_fourcc_ = static_cast<int>(cap_.get(cv::CAP_PROP_FOURCC));
    raw_mode_ = first.channels() != 3;

    if (fourcc_str(actual_fourcc_) != "MJPG") {
        log_msg("Камера вернула FOURCC=" + fourcc_str(actual_fourcc_) +
                    ", а не MJPG — FPS может быть низким",
                "WARN");
    }
    if (actual_w_ != width || actual_h_ != height) {
        log_msg("Запрошено " + std::to_string(width) + "x" +
                    std::to_string(height) + ", камера дала " +
                    std::to_string(actual_w_) + "x" +
                    std::to_string(actual_h_),
                "WARN");
    }
}

RawCamera::~RawCamera() { release(); }

bool RawCamera::read(cv::Mat& out) { return cap_.read(out) && !out.empty(); }

void RawCamera::release() {
    if (opened_) {
        try {
            cap_.release();
        } catch (...) {
        }
        opened_ = false;
    }
}

CaptureThread::CaptureThread(RawCamera& camera) : camera_(camera) {}

CaptureThread::~CaptureThread() { stop(); }

void CaptureThread::start() {
    thread_ = std::thread([this] { run(); });
}

void CaptureThread::stop() {
    // cap.read() returns within ~1 frame period (or errors out), so a plain
    // join is bounded in practice (Python joined with timeout=1.5 s for the
    // same reason).
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
}

void CaptureThread::run() {
    t0_ = std::chrono::steady_clock::now();
    while (!stop_.load()) {
        cv::Mat frame;
        if (!camera_.read(frame)) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                ++errors_;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        // Snapshot for the main thread. Clone so the backend cannot touch the
        // buffer we hand out (same as Python data.copy()).
        {
            std::lock_guard<std::mutex> lock(mu_);
            latest_ = frame.clone();
            ++seq_;
            ++frames_;
        }
    }
}

bool CaptureThread::get_latest(long long last_seq, cv::Mat& out,
                               long long& new_seq) {
    std::lock_guard<std::mutex> lock(mu_);
    if (seq_ == last_seq || latest_.empty()) return false;
    out = latest_.clone();  // main-thread copy; capture thread keeps writing
    new_seq = seq_;
    return true;
}

double CaptureThread::fps() const {
    // Python measures frames / seconds-since-thread-start (long-run average).
    const auto t0 = t0_;
    if (t0.time_since_epoch().count() == 0) return 0.0;
    const double dt =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    if (dt < 1e-6) return 0.0;
    std::lock_guard<std::mutex> lock(mu_);
    return frames_ / dt;
}

long long CaptureThread::errors() const {
    std::lock_guard<std::mutex> lock(mu_);
    return errors_;
}

}  // namespace pi_core
