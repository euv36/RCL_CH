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

#include "cam_settings_config.hpp"
#include "field_map.hpp"
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

    // обрабатывает мышь во вспомогательных окнах.
    void handle_aux_mouse(const std::string& window, int event, int x, int y);

    // Строит кадры вспомогательных окон без вывода на экран.
    cv::Mat build_radar_frame() const;
    cv::Mat build_hsv_frame() const;
    cv::Mat build_help_frame() const;

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
    void send_cam_settings();
    void calibrate_lab(int delta);
    void bind_field();
    void set_gate_role(int slot, const std::string& role);
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
    bool cam_window_initialized_ = false;
    int cam_view_w_ = 640;
    int cam_view_h_ = 480;

    std::array<GateTrackResult, 3> track_results_{};
    std::array<cv::Mat, 3> gate_masks_{};
    ParsedStatus status_{};
    std::string status_json_;
    std::chrono::steady_clock::time_point status_received_at_{};
    bool has_status_ = false;
    CvLog log_;
    FieldMap field_map_;
    CamSettingsConfig cam_config_;
    cv::VideoCapture capture_;
    cv::Point selection_start_{};
    cv::Point selection_current_{};
    bool selecting_ = false;
    std::chrono::steady_clock::time_point app_start_ =
        std::chrono::steady_clock::now();

    int frames_processed_ = 0;
    int selected_threshold_slot_ = 0;
    int target_gate_ = 1;
    int help_tab_ = 0;
    bool quit_requested_ = false;
    bool need_connect_ = false;
    bool colors_dirty_ = true;
    std::chrono::steady_clock::time_point last_color_send_{};
    std::chrono::steady_clock::time_point last_cam_send_{};
};

} // namespace camera_cpp::laptop
