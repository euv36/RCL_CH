#include "pi_camera/v4l2_capture.hpp"
#include "vision/tracking.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {

// Создаёт портретный кадр 720x1280 из рабочего кадра Pi 640x360 поворотом
// на 90 градусов против часовой стрелки с масштабом 2. Без обрезки поля зрения.
cv::Mat to_portrait(const cv::Mat& processing, cv::Mat& rotated, cv::Mat& portrait) {
    if (processing.empty()) return {};
    cv::rotate(processing, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::resize(rotated, portrait, {720, 1280}, 0.0, 0.0, cv::INTER_AREA);
    return portrait;
}

// Порог в 8-битном OpenCV Lab для жёлтого цвета ворот из legacy-палитры.
camera_cpp::GateThreshold yellow_threshold() {
    cv::Mat pixel(1, 1, CV_8UC3, cv::Scalar(255, 255, 0));
    cv::cvtColor(pixel, pixel, cv::COLOR_RGB2Lab);
    const cv::Vec3b lab = pixel.at<cv::Vec3b>(0, 0);
    camera_cpp::GateThreshold threshold;
    threshold.l_min = std::max(0, static_cast<int>(lab[0]) - 30);
    threshold.l_max = std::min(255, static_cast<int>(lab[0]) + 30);
    threshold.a_min = std::max(0, static_cast<int>(lab[1]) - 40);
    threshold.a_max = std::min(255, static_cast<int>(lab[2]) + 40);
    threshold.b_min = std::max(0, static_cast<int>(lab[2]) - 40);
    threshold.b_max = std::min(255, static_cast<int>(lab[2]) + 40);
    threshold.configured = true;
    return threshold;
}

int int_arg(int argc, char** argv, const char* name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) return std::atoi(argv[i + 1]);
    }
    return fallback;
}

} // namespace

// Headless-проба локальной USB-камеры без GUI, ZMQ и Pi.
// Проверяет полный путь кадра: чтение -> 640x360 -> портрет 720x1280 -> трекинг.
int main(int argc, char** argv) {
    const int index = int_arg(argc, argv, "--index", 0);
    const int max_frames = int_arg(argc, argv, "--frames", 30);
    const int width = int_arg(argc, argv, "--width", 1920);
    const int height = int_arg(argc, argv, "--height", 1080);
    const int fps = int_arg(argc, argv, "--fps", 30);

    cv::VideoCapture capture;
    capture.open(index, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        std::cerr << "cannot open local camera index " << index << std::endl;
        return 1;
    }
    const int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    capture.set(cv::CAP_PROP_FOURCC, static_cast<double>(fourcc));
    capture.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(width));
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(height));
    capture.set(cv::CAP_PROP_FPS, static_cast<double>(fps));
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1.0);

    const int negotiated_width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int negotiated_height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const int negotiated_fps = static_cast<int>(capture.get(cv::CAP_PROP_FPS));
    std::cout << "camera_index=" << index
              << " negotiated=" << negotiated_width << "x" << negotiated_height
              << " fps=" << negotiated_fps << std::endl;

    camera_cpp::TrackingEngine engine;
    camera_cpp::TrackInput input;
    input.thresholds[0] = yellow_threshold();

    cv::Mat processing;
    cv::Mat rotated;
    cv::Mat portrait;
    int frames_read = 0;
    int frames_tracked = 0;
    int gates_found = 0;
    const auto started = std::chrono::steady_clock::now();

    for (int i = 0; i < max_frames; ++i) {
        cv::Mat frame;
        if (!capture.read(frame) || frame.empty()) break;
        ++frames_read;

        camera_cpp::downscale_and_rotate(frame, processing);
        if (processing.empty()) break;
        const cv::Mat view = to_portrait(processing, rotated, portrait);
        if (view.empty()) break;

        input.portrait_rgb = &view;
        const auto results = engine.track(input);
        ++frames_tracked;
        if (results[0].found) ++gates_found;
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto stages = engine.last_stage_ms();
    std::cout << "frames_read=" << frames_read
              << " frames_tracked=" << frames_tracked
              << " gates_found=" << gates_found
              << " elapsed_s=" << elapsed
              << " observed_fps=" << (elapsed > 0.0 ? frames_read / elapsed : 0.0) << std::endl;
    std::cout << "stage_ms cvt=" << stages.cvt_ms
              << " mask=" << stages.mask_ms
              << " cc=" << stages.cc_ms
              << " cluster=" << stages.cluster_ms
              << " total=" << stages.total_ms << std::endl;
    std::cout << "processing=" << processing.cols << "x" << processing.rows
              << " portrait=" << portrait.cols << "x" << portrait.rows << std::endl;

    capture.release();
    return frames_read > 0 ? 0 : 2;
}
