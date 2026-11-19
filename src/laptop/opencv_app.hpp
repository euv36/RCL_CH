#pragma once

#include <array>
#include <chrono>
#include <string>
#include <string_view>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include "laptop/cv_widgets.hpp"
#include "laptop/gate_publisher.hpp"
#include "laptop/pi_video_client.hpp"
#include "laptop/state.hpp"
#include "laptop/status_panel_client.hpp"

#include "gate_store.hpp"
#include "tracking.hpp"

namespace camera_cpp::laptop {

// Главное окно системы на OpenCV HighGUI: видео Pi, трекинг и все панели.
class OpenCVApp {
public:
    OpenCVApp();

    // Запускает цикл окна; возвращает 0 при нормальном выходе.
    int run();

    // Обрабатывает RGB-кадр: трекинг, статус и обновление состояния панелей.
    void process_frame(const cv::Mat& rgb);

    // Текущий текст поля источника (IP или индекс камеры).
    std::string source_input() const;

    // Добавляет текст в поле источника.
    void append_source_text(std::string_view text);

    // Убирает последний символ из поля источника.
    void backspace_source();

    // Обрабатывает нажатие клавиши HighGUI.
    void handle_key(int key);

    // Обрабатывает событие мыши HighGUI.
    void handle_mouse(int event, int x, int y, int flags);

    AppState& state();
    const AppState& state() const;

    bool has_frame() const noexcept;
    cv::Size last_frame_size() const noexcept;
    int frames_processed() const noexcept;

private:
    void load_saved_gates();
    void refresh_tracking();
    void apply_status(const ParsedStatus& status);
    void connect_source();
    void save_gate(int slot);
    void publish_colors(bool force = false);
    void render_main();
    void render_radar();
    void render_hsv();
    void render_cam_settings();
    void render_help();

    AppState state_;
    GateStore gate_store_;
    TrackingEngine tracker_;
    GatePublisher publisher_;
    PiVideoClient video_client_;

    cv::Mat source_frame_;
    cv::Mat portrait_frame_;
    cv::Mat processed_frame_;
    cv::Mat hsv_frame_;
    cv::Mat canvas_;
    cv::Mat radar_canvas_;
    cv::Mat cam_canvas_;
    cv::Mat help_canvas_;

    std::array<GateTrackResult, 3> track_results_{};
    std::array<cv::Mat, 3> gate_masks_{};
    ParsedStatus status_{};
    CvLog log_;
    cv::VideoCapture capture_;
    cv::Point selection_start_{};
    cv::Point selection_current_{};
    bool selecting_ = false;
    std::chrono::steady_clock::time_point app_start_ =
        std::chrono::steady_clock::now();

    int frames_processed_ = 0;
    int selected_threshold_slot_ = 0;
    int target_gate_ = 1;
    bool quit_requested_ = false;
    bool need_connect_ = false;
    bool colors_dirty_ = true;
    std::chrono::steady_clock::time_point last_color_send_{};
};

} // namespace camera_cpp::laptop
