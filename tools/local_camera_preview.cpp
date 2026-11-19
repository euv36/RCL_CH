#define SDL_MAIN_HANDLED

#include "laptop/preview_support.hpp"
#include "pi_camera/v4l2_capture.hpp"
#include "vision/tracking.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <SDL.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {

// Порог 8-битного OpenCV Lab для жёлтых ворот legacy-палитры.
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

// Поворачивает рабочий кадр Pi и масштабирует его до портрета 720x1280.
void make_portrait(const cv::Mat& processing, cv::Mat& rotated, cv::Mat& portrait) {
    cv::rotate(processing, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::resize(rotated, portrait, {720, 1280}, 0.0, 0.0, cv::INTER_AREA);
}

// Рисует найденные ворота поверх картинки, не меняя сам кадр.
void draw_gates(SDL_Renderer* renderer, const camera_cpp::laptop::PreviewRect& rect,
                const cv::Mat& portrait,
                const std::array<camera_cpp::GateTrackResult, 3>& results) {
    if (portrait.empty() || rect.w <= 0 || rect.h <= 0) return;
    const float scale_x = static_cast<float>(rect.w) / static_cast<float>(portrait.cols);
    const float scale_y = static_cast<float>(rect.h) / static_cast<float>(portrait.rows);
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    for (const auto& gate : results) {
        if (!gate.found || gate.bbox.empty()) continue;
        SDL_Rect box;
        box.x = rect.x + static_cast<int>(static_cast<float>(gate.bbox.x) * scale_x);
        box.y = rect.y + static_cast<int>(static_cast<float>(gate.bbox.y) * scale_y);
        box.w = std::max(2, static_cast<int>(static_cast<float>(gate.bbox.width) * scale_x));
        box.h = std::max(2, static_cast<int>(static_cast<float>(gate.bbox.height) * scale_y));
        SDL_RenderDrawRect(renderer, &box);
    }
}

} // namespace

// Локальный предпросмотр камеры ПК в окне SDL2 без Pi, ZMQ и сети.
int main(int argc, char** argv) {
    const int index = int_arg(argc, argv, "--index", 0);
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

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        capture.release();
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("Local camera preview", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 900, 1000, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (!window || !renderer) {
        std::cerr << "SDL window/renderer failed: " << SDL_GetError() << std::endl;
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        capture.release();
        return 3;
    }

    camera_cpp::TrackingEngine engine;
    camera_cpp::TrackInput input;
    input.thresholds[0] = yellow_threshold();

    cv::Mat processing;
    cv::Mat rotated;
    cv::Mat portrait;
    SDL_Texture* texture = nullptr;
    int frames = 0;
    int gates_found = 0;
    const auto started = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        cv::Mat frame;
        std::array<camera_cpp::GateTrackResult, 3> results{};
        if (capture.read(frame) && !frame.empty()) {
            camera_cpp::downscale_and_rotate(frame, processing);
            if (!processing.empty()) {
                make_portrait(processing, rotated, portrait);
                input.portrait_rgb = &portrait;
                results = engine.track(input);
                ++frames;
                for (const auto& gate : results) {
                    if (gate.found) ++gates_found;
                }

                const camera_cpp::laptop::RgbaImage image =
                    camera_cpp::laptop::bgr_to_rgba(portrait);
                if (!texture && image.width > 0 && image.height > 0) {
                    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                image.width, image.height);
                }
                if (texture && !image.pixels.empty()) {
                    SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * 4);
                }
            }
        }

        int window_w = 0;
        int window_h = 0;
        SDL_GetWindowSize(window, &window_w, &window_h);
        const auto rect = camera_cpp::laptop::fit_rect(window_w, window_h,
                                                       portrait.cols, portrait.rows);
        SDL_RenderClear(renderer);
        if (texture && rect.w > 0 && rect.h > 0) {
            const SDL_Rect dst{rect.x, rect.y, rect.w, rect.h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            draw_gates(renderer, rect, portrait, results);
        }
        SDL_RenderPresent(renderer);

        if (frames > 0 && frames % 30 == 0) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            const auto stages = engine.last_stage_ms();
            std::cout << "frames=" << frames << " fps=" << (frames / elapsed)
                      << " gates=" << gates_found
                      << " stage_ms=" << stages.total_ms << std::endl;
        }
    }

    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    capture.release();
    return 0;
}
