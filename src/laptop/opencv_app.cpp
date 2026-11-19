#include "laptop/opencv_app.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

#include "cam_settings_config.hpp"
#include "clipboard_text.hpp"
#include "contracts.hpp"
#include "help_ru.hpp"
#include "hsv_color.hpp"
#include "hsv_panel.hpp"
#include "preview_support.hpp"
#include "source_controller.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace camera_cpp::laptop {

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr const char* kMainWindow = "Camera";
constexpr const char* kRadarWindow = "RADAR";
constexpr const char* kHsvWindow = "HSV MASK";
constexpr const char* kCamWindow = "CAM SET";
constexpr const char* kHelpWindow = "INFO";

using clock = std::chrono::steady_clock;

// Переводит legacy L (0..100) в 8-битный OpenCV Lab.
int lab_l_to_u8(int value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0 / 100.0)), 0, 255);
}

// Переводит legacy a/b (-128..127) в 8-битный OpenCV Lab со сдвигом 128.
int lab_ab_to_u8(int value) {
    return std::clamp(static_cast<int>(std::lround(value + 128.0)), 0, 255);
}

// Строит порог трекинга из сохранённого LAB-диапазона ворот.
GateThreshold threshold_from_lab(const GateLab& lab) {
    GateThreshold threshold;
    threshold.l_min = lab_l_to_u8(lab[0]);
    threshold.l_max = lab_l_to_u8(lab[1]);
    threshold.a_min = lab_ab_to_u8(lab[2]);
    threshold.a_max = lab_ab_to_u8(lab[3]);
    threshold.b_min = lab_ab_to_u8(lab[4]);
    threshold.b_max = lab_ab_to_u8(lab[5]);
    if (threshold.l_min > threshold.l_max) std::swap(threshold.l_min, threshold.l_max);
    if (threshold.a_min > threshold.a_max) std::swap(threshold.a_min, threshold.a_max);
    if (threshold.b_min > threshold.b_max) std::swap(threshold.b_min, threshold.b_max);
    threshold.configured = true;
    return threshold;
}

// Выводит tolerантный порог из цвета ворот, если LAB-диапазон не сохранён.
GateThreshold threshold_from_color(const std::array<int, 3>& color) {
    const uint8_t r = static_cast<uint8_t>(std::clamp(color[0], 0, 255));
    const uint8_t g = static_cast<uint8_t>(std::clamp(color[1], 0, 255));
    const uint8_t b = static_cast<uint8_t>(std::clamp(color[2], 0, 255));
    const RgbLab lab = rgb_to_lab_u8(r, g, b);
    GateLab legacy{};
    legacy[0] = std::clamp(static_cast<int>(std::lround(lab.L)) - 10, 0, 100);
    legacy[1] = std::clamp(static_cast<int>(std::lround(lab.L)) + 10, 0, 100);
    legacy[2] = std::clamp(static_cast<int>(std::lround(lab.A)) - 15, -128, 127);
    legacy[3] = std::clamp(static_cast<int>(std::lround(lab.A)) + 15, -128, 127);
    legacy[4] = std::clamp(static_cast<int>(std::lround(lab.B)) - 15, -128, 127);
    legacy[5] = std::clamp(static_cast<int>(std::lround(lab.B)) + 15, -128, 127);
    return threshold_from_lab(legacy);
}

// Читает системный буфер обмена; непечатные символы отбрасываются ниже.
std::string clipboard_utf8() {
#if defined(_WIN32)
    if (!OpenClipboard(nullptr)) return {};
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) {
        CloseClipboard();
        return {};
    }
    const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
    if (data == nullptr) {
        CloseClipboard();
        return {};
    }
    const std::wstring wide(data);
    GlobalUnlock(handle);
    CloseClipboard();

    std::string utf8;
    for (wchar_t ch : wide) {
        if (ch <= 0x7F) {
            utf8.push_back(static_cast<char>(ch));
        } else if (ch <= 0x7FF) {
            utf8.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return sanitize_clipboard_bytes(utf8);
#else
    return {};
#endif
}

CvRect input_rect() { return {1000, 55, 170, 34}; }
CvRect connect_rect() { return {1175, 55, 90, 34}; }
CvRect status_rect() { return {1000, 100, 265, 100}; }
CvRect log_rect() { return {1000, 210, 265, 240}; }

// Верхний ряд кнопок: режимы, пауза и дополнительные окна.
std::array<CvButton, 11> top_buttons() {
    const std::array<const char*, 11> labels = {
        "BITMAP", "L", "A", "B", "PAUSE", "CAM SET", "RADAR", "BIND", "POS", "HSV", "INFO"};
    const std::array<int, 11> widths = {82, 36, 36, 36, 48, 85, 62, 62, 40, 56, 52};
    std::array<CvButton, 11> buttons{};
    int x = 20;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].rect = {x, 10, widths[i], 34};
        buttons[i].label = labels[i];
        x += widths[i] + 6;
    }
    return buttons;
}

// Нижние слайдеры LAB: по два на каждый канал.
std::array<CvSlider, 6> bottom_sliders() {
    std::array<CvSlider, 6> sliders{};
    const std::array<const char*, 6> labels = {"L min", "L max", "A min", "A max", "B min", "B max"};
    for (int i = 0; i < 6; ++i) {
        sliders[i].label = labels[i];
        sliders[i].rect = {30 + (i / 2) * 350, 660, 260, 12};
        sliders[i].min = (i >= 2) ? -128 : 0;
        sliders[i].max = (i >= 2) ? 127 : 100;
    }
    return sliders;
}

// Кнопки сохранения ворот внизу окна.
std::array<CvButton, 4> bottom_buttons() {
    std::array<CvButton, 4> buttons{};
    buttons[0].rect = {30, 610, 130, 32};
    buttons[0].label = "TARGET G1";
    buttons[1].rect = {170, 610, 100, 32};
    buttons[1].label = "SAVE G1";
    buttons[2].rect = {280, 610, 100, 32};
    buttons[2].label = "SAVE G2";
    buttons[3].rect = {390, 610, 100, 32};
    buttons[3].label = "SAVE G3";
    return buttons;
}

// Области двух квадратных превью: исходный кадр и результат трекинга.
std::array<CvRect, 2> preview_areas() {
    return {CvRect{20, 60, 468, 520}, CvRect{500, 60, 468, 520}};
}

// Возвращает индекс кнопки под курсором или -1.
int button_at(const std::array<CvButton, 11>& buttons, int x, int y) {
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        if (buttons[i].rect.contains(x, y)) return i;
    }
    return -1;
}

// Кнопки окна CAM SET.
std::array<CvButton, 5> cam_buttons() {
    const std::array<const char*, 5> labels = {
        "SAFE DEFAULTS", "SAVE NOW", "SEND TO PI", "ROLE OURS", "ROLE OPP"};
    const std::array<int, 5> widths = {130, 96, 100, 96, 96};
    std::array<CvButton, 5> buttons{};
    int x = 12;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].rect = {x, 380, widths[i], 28};
        buttons[i].label = labels[i];
        x += widths[i] + 6;
    }
    return buttons;
}

// Вкладки справки INFO: идентификаторы разделов на латинице для HighGUI.
std::array<CvButton, 7> help_tabs() {
    const auto& sections = HelpRu::sections();
    std::array<CvButton, 7> buttons{};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].rect = {10 + static_cast<int>(i) * 126, 10, 120, 28};
        buttons[i].label = i < sections.size() ? sections[i].id : "";
    }
    return buttons;
}

// Индекс кнопки под курсором или -1.
template <std::size_t N>
int find_button(const std::array<CvButton, N>& buttons, int x, int y) {
    for (std::size_t i = 0; i < N; ++i) {
        if (buttons[i].rect.contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

// Статический мост HighGUI → экземпляр приложения.
void on_mouse(int event, int x, int y, int flags, void* userdata) {
    auto* app = static_cast<OpenCVApp*>(userdata);
    if (app != nullptr) app->handle_mouse(event, x, y, flags);
}

// Мосты для вспомогательных окон; имя окна передаётся в приложение.
void on_cam_mouse(int event, int x, int y, int flags, void* userdata) {
    (void)flags;
    auto* app = static_cast<OpenCVApp*>(userdata);
    if (app != nullptr) app->handle_aux_mouse("CAM SET", event, x, y);
}

void on_help_mouse(int event, int x, int y, int flags, void* userdata) {
    (void)flags;
    auto* app = static_cast<OpenCVApp*>(userdata);
    if (app != nullptr) app->handle_aux_mouse("INFO", event, x, y);
}

} // namespace

OpenCVApp::OpenCVApp() {
    canvas_ = cv::Mat(kWindowH, kWindowW, CV_8UC3, cv::Scalar(18, 18, 22));
    log_.push("Camera UI: CONNECT to Pi IP or camera index");
}

std::string OpenCVApp::source_input() const {
    return state_.camera_source_text;
}

void OpenCVApp::append_source_text(std::string_view text) {
    laptop::append_source_text(state_, text);
}

void OpenCVApp::backspace_source() {
    laptop::handle_source_backspace(state_);
}

AppState& OpenCVApp::state() { return state_; }
const AppState& OpenCVApp::state() const { return state_; }

bool OpenCVApp::has_frame() const noexcept { return !source_frame_.empty(); }
cv::Size OpenCVApp::last_frame_size() const noexcept { return source_frame_.size(); }
int OpenCVApp::frames_processed() const noexcept { return frames_processed_; }

void OpenCVApp::load_saved_gates() {
    const std::string path = (std::filesystem::path("saved_gates.json")).string();
    if (!gate_store_.load(path)) {
        log_.push("saved_gates.json not found; gates disabled");
        return;
    }
    for (int i = 0; i < 3; ++i) {
        const GateId id = static_cast<GateId>(i + 1);
        const auto record = gate_store_.gate(id);
        state_.gate_enabled[i] = gate_store_.enabled(id);
        if (record.has_value()) state_.gate_colors[i] = record->rgb;
    }
    colors_dirty_ = true;
    log_.push("saved gates restored");
}

void OpenCVApp::refresh_tracking() {
    if (portrait_frame_.empty()) return;
    TrackInput input;
    input.portrait_rgb = &portrait_frame_;
    for (int i = 0; i < 3; ++i) {
        if (!state_.gate_enabled[i]) continue;
        const auto record = gate_store_.gate(static_cast<GateId>(i + 1));
        input.thresholds[i] = record && record->lab.has_value()
            ? threshold_from_lab(*record->lab)
            : threshold_from_color(state_.gate_colors[i]);
    }
    track_results_ = tracker_.track(input);
    for (int i = 0; i < 3; ++i) gate_masks_[i] = track_results_[i].mask;

    processed_frame_ = portrait_frame_.clone();
    for (const auto& result : track_results_) {
        if (!result.found || result.bbox.width <= 0) continue;
        cv::rectangle(processed_frame_, result.bbox, cv::Scalar(40, 220, 90), 2);
        const std::string label = "G" + std::to_string(&result - track_results_.data() + 1) +
                                  " " + std::to_string(static_cast<int>(result.angle_deg)) + "deg";
        cv::putText(processed_frame_, label, {result.bbox.x, result.bbox.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(40, 220, 90), 1);
    }
}

void OpenCVApp::apply_status(const ParsedStatus& status) {
    if (!status.valid) return;
    if (status.stale) {
        state_.pi_status_summary = "Pi: STALE status";
        return;
    }
    state_.fps_ui = status.fps;
    std::ostringstream out;
    out << "Pi " << status.src << "  fps " << status.fps;
    state_.pi_status_summary = out.str();
    for (int i = 0; i < 3; ++i) {
        RadarGate& radar = state_.radar_gates[i];
        radar.id = status.gates[i].id;
        radar.enabled = state_.gate_enabled[i];
        radar.found = status.gates[i].found && radar.enabled;
        radar.angle_deg = status.gates[i].angle_deg;
        radar.distance_cm = status.gates[i].dist_cm;
        radar.rgb = state_.gate_colors[i];
    }
}

void OpenCVApp::process_frame(const cv::Mat& rgb) {
    if (rgb.empty()) return;
    source_frame_ = rgb.clone();

    cv::Mat rotated;
    cv::rotate(rgb, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
    const cv::Size target(contracts::kLaptopPortraitW, contracts::kLaptopPortraitH);
    const int interpolation = rotated.cols > target.width || rotated.rows > target.height
        ? cv::INTER_AREA : cv::INTER_LINEAR;
    cv::resize(rotated, portrait_frame_, target, 0, 0, interpolation);

    ++frames_processed_;
    refresh_tracking();
}

void OpenCVApp::connect_source() {
    need_connect_ = false;
    const auto request = parse_connect(state_.camera_source_text);
    if (!request.valid()) {
        state_.connect_error = request.error;
        log_.push("CONNECT failed: " + request.error);
        return;
    }
    apply_connect(state_, request);
    video_client_.disconnect();
    capture_.release();
    if (request.kind == AppState::SourceKind::Pi) {
        if (!video_client_.connect(request.host, request.port)) {
            state_.connect_error = "Cannot connect to Pi";
            log_.push("Cannot connect to Pi " + request.host);
        } else {
            state_.connect_error.clear();
            log_.push("Connected to Pi " + request.host);
        }
    } else if (request.kind == AppState::SourceKind::Local) {
        capture_.open(request.camera_index, cv::CAP_ANY);
        capture_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
        if (!capture_.isOpened()) {
            state_.connect_error = "Cannot open local camera";
            log_.push("Cannot open camera " + std::to_string(request.camera_index));
        } else {
            state_.connect_error.clear();
            log_.push("Local camera " + std::to_string(request.camera_index));
        }
    }
}

void OpenCVApp::save_gate(int slot) {
    if (slot < 1 || slot > 3 || portrait_frame_.empty()) return;
    const std::size_t index = static_cast<std::size_t>(slot - 1);

    cv::Vec3b mean_color{0, 0, 0};
    if (selecting_ && selection_start_.x >= 0) {
        const auto fitted = fit_rect(468, 520, portrait_frame_.cols, portrait_frame_.rows);
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!preview_point_to_image(fitted, selection_start_.x, selection_start_.y,
                                    portrait_frame_.cols, portrait_frame_.rows, x1, y1) ||
            !preview_point_to_image(fitted, selection_current_.x, selection_current_.y,
                                    portrait_frame_.cols, portrait_frame_.rows, x2, y2)) {
            selecting_ = false;
            log_.push("SAVE G" + std::to_string(slot) + " selection outside");
            return;
        }
        const cv::Rect selection(std::min(x1, x2), std::min(y1, y2),
                                 std::abs(x2 - x1) + 1, std::abs(y2 - y1) + 1);
        const cv::Rect clipped = selection & cv::Rect(0, 0, portrait_frame_.cols, portrait_frame_.rows);
        if (clipped.width <= 0 || clipped.height <= 0) {
            selecting_ = false;
            log_.push("SAVE G" + std::to_string(slot) + " selection empty");
            return;
        }
        const cv::Scalar mean = cv::mean(portrait_frame_(clipped));
        mean_color = cv::Vec3b(static_cast<uint8_t>(mean[0]), static_cast<uint8_t>(mean[1]),
                               static_cast<uint8_t>(mean[2]));
        selecting_ = false;
    } else {
        const cv::Rect center(portrait_frame_.cols / 2 - 20, portrait_frame_.rows / 2 - 20, 40, 40);
        const cv::Scalar mean = cv::mean(portrait_frame_(center));
        mean_color = cv::Vec3b(static_cast<uint8_t>(mean[0]), static_cast<uint8_t>(mean[1]),
                               static_cast<uint8_t>(mean[2]));
    }

    state_.gate_colors[index] = {mean_color[0], mean_color[1], mean_color[2]};
    const RgbLab lab = rgb_to_lab_u8(mean_color[0], mean_color[1], mean_color[2]);
    GateLab legacy{};
    legacy[0] = std::clamp(static_cast<int>(std::lround(lab.L)) - 10, 0, 100);
    legacy[1] = std::clamp(static_cast<int>(std::lround(lab.L)) + 10, 0, 100);
    legacy[2] = std::clamp(static_cast<int>(std::lround(lab.A)) - 15, -128, 127);
    legacy[3] = std::clamp(static_cast<int>(std::lround(lab.A)) + 15, -128, 127);
    legacy[4] = std::clamp(static_cast<int>(std::lround(lab.B)) - 15, -128, 127);
    legacy[5] = std::clamp(static_cast<int>(std::lround(lab.B)) + 15, -128, 127);

    gate_store_.upsert(static_cast<GateId>(slot),
                       {state_.gate_colors[index][0], state_.gate_colors[index][1],
                        state_.gate_colors[index][2]},
                       "unset", legacy);
    state_.gate_enabled[index] = true;
    const bool saved = gate_store_.save_atomic();
    state_.gate_save_status = saved ? "G" + std::to_string(slot) + " saved"
                                    : "G" + std::to_string(slot) + " save failed";
    log_.push(state_.gate_save_status);
    colors_dirty_ = true;
}

void OpenCVApp::publish_colors(bool force) {
    if (!publisher_.bound()) return;
    const auto now = clock::now();
    const bool restore_window = now - app_start_ < std::chrono::seconds(10);
    const bool periodic = restore_window && now - last_color_send_ >= std::chrono::seconds(2);
    if (!force && !colors_dirty_ && !periodic) return;

    std::vector<GateColorOut> out;
    const auto roles = gate_store_.roles();
    for (int i = 0; i < 3; ++i) {
        if (!state_.gate_enabled[i]) continue;
        out.push_back({i + 1, state_.gate_colors[i], roles[i]});
    }
    publisher_.publish_gates(out);
    last_color_send_ = now;
    colors_dirty_ = false;
}

void OpenCVApp::render_main() {
    canvas_.setTo(cv::Scalar(18, 18, 22));
    const auto buttons = top_buttons();
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        bool active = false;
        if (i == 0) active = state_.view_mode == AppState::ViewMode::Bitmap;
        if (i == 1) active = state_.view_mode == AppState::ViewMode::LabL;
        if (i == 2) active = state_.view_mode == AppState::ViewMode::LabA;
        if (i == 3) active = state_.view_mode == AppState::ViewMode::LabB;
        if (i == 4) active = state_.paused;
        if (i == 5) active = state_.show_cam_settings;
        if (i == 6) active = state_.show_radar;
        if (i == 8 && state_.position_preset == 1) active = true;
        if (i == 9) active = state_.show_hsv;
        if (i == 10) active = state_.show_help;
        buttons[i].draw(canvas_, active);
    }

    cv::Mat left_view = portrait_frame_;
    if (!portrait_frame_.empty() && state_.view_mode != AppState::ViewMode::Bitmap) {
        cv::Mat lab;
        cv::cvtColor(portrait_frame_, lab, cv::COLOR_RGB2Lab);
        std::vector<cv::Mat> channels;
        cv::split(lab, channels);
        int channel = 0;
        if (state_.view_mode == AppState::ViewMode::LabA) channel = 1;
        if (state_.view_mode == AppState::ViewMode::LabB) channel = 2;
        cv::Mat gray = channels[channel].clone();
        cv::cvtColor(gray, left_view, cv::COLOR_GRAY2RGB);
    }

    const auto areas = preview_areas();
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const cv::Mat& image = i == 0 ? left_view : processed_frame_;
        cv::rectangle(canvas_, {areas[i].x, areas[i].y},
                      {areas[i].x + areas[i].w, areas[i].y + areas[i].h},
                      cv::Scalar(80, 80, 80), 1);
        if (!image.empty()) {
            const auto fitted = fit_rect(areas[i].w, areas[i].h, image.cols, image.rows);
            cv::Mat resized;
            cv::resize(image, resized, {fitted.w, fitted.h}, 0, 0, cv::INTER_AREA);
            resized.copyTo(canvas_(cv::Rect(areas[i].x + fitted.x, areas[i].y + fitted.y,
                                            fitted.w, fitted.h)));
        }
    }
    if (selecting_ && selection_start_.x >= 0) {
        const auto preview = preview_areas();
        const cv::Point a(preview[0].x + selection_start_.x, preview[0].y + selection_start_.y);
        const cv::Point b(preview[0].x + selection_current_.x, preview[0].y + selection_current_.y);
        cv::rectangle(canvas_, a, b, cv::Scalar(0, 220, 0), 1);
    }

    CvTextInput input;
    input.rect = input_rect();
    input.text = state_.camera_source_text;
    input.active = state_.input_active;
    input.draw(canvas_);
    CvButton connect;
    connect.rect = connect_rect();
    connect.label = "CONNECT";
    connect.draw(canvas_, false);

    cv::putText(canvas_, state_.pi_status_summary, {1000, 92}, cv::FONT_HERSHEY_SIMPLEX,
                0.45, cv::Scalar(160, 220, 160), 1);
    if (!state_.connect_error.empty()) {
        cv::putText(canvas_, state_.connect_error, {1000, 110}, cv::FONT_HERSHEY_SIMPLEX,
                    0.4, cv::Scalar(80, 80, 230), 1);
    }
    cv::rectangle(canvas_, {status_rect().x, status_rect().y},
                  {status_rect().x + status_rect().w, status_rect().y + status_rect().h},
                  cv::Scalar(80, 80, 80), 1);
    log_.draw(canvas_, log_rect());

    const auto bottom = bottom_buttons();
    for (const auto& button : bottom) button.draw(canvas_, false);
    cv::putText(canvas_, "TARGET G" + std::to_string(target_gate_), {520, 630},
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 220, 200), 1);

    const auto sliders = bottom_sliders();
    for (const auto& slider : sliders) slider.draw(canvas_);

    cv::imshow(kMainWindow, canvas_);
}

cv::Mat OpenCVApp::build_radar_frame() const {
    cv::Mat frame(440, 440, CV_8UC3, cv::Scalar(15, 20, 15));
    const cv::Point center(220, 250);
    for (int r = 60; r <= 180; r += 60) {
        cv::circle(frame, center, r, cv::Scalar(50, 90, 50), 1);
    }
    cv::line(frame, {center.x - 200, center.y}, {center.x + 200, center.y},
             cv::Scalar(50, 90, 50), 1);
    cv::line(frame, {center.x, center.y - 200}, {center.x, center.y + 200},
             cv::Scalar(50, 90, 50), 1);

    const auto roles = gate_store_.roles();
    for (const auto& gate : state_.radar_gates) {
        const int slot = gate.id - 1;
        if (slot < 0 || slot > 2 || !radar_gate_visible(gate)) continue;
        const double radians = gate.angle_deg * CV_PI / 180.0;
        const int radius = std::clamp(static_cast<int>(gate.distance_cm / 5.0F), 20, 180);
        const cv::Point point(center.x + static_cast<int>(std::sin(radians) * radius),
                              center.y - static_cast<int>(std::cos(radians) * radius));
        const cv::Scalar color(gate.rgb[2], gate.rgb[1], gate.rgb[0]);
        cv::circle(frame, point, 7, color, cv::FILLED);
        std::string tag = "G" + std::to_string(gate.id);
        if (roles[slot] == "ours") tag += " OURS";
        else if (roles[slot] == "opponent") tag += " OPP";
        cv::putText(frame, tag, {point.x + 9, point.y - 9}, cv::FONT_HERSHEY_SIMPLEX,
                    0.4, color, 1);
    }

    const std::string advice = state_.bind_field_status.empty()
        ? "EDGE: BIND FIELD to estimate" : "EST: " + state_.bind_field_status;
    cv::putText(frame, advice, {12, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cv::Scalar(160, 220, 160), 1);
    cv::putText(frame, "RADAR  POS " + std::to_string(state_.position_preset),
                {12, 425}, cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(180, 180, 180), 1);
    return frame;
}

void OpenCVApp::render_radar() {
    radar_canvas_ = build_radar_frame();
    cv::imshow(kRadarWindow, radar_canvas_);
}

cv::Mat OpenCVApp::build_hsv_frame() const {
    HsvPalette palette;
    for (int i = 0; i < 3; ++i) {
        palette.gate_colors[i] = cv::Vec3b(
            static_cast<uchar>(state_.gate_colors[i][0]),
            static_cast<uchar>(state_.gate_colors[i][1]),
            static_cast<uchar>(state_.gate_colors[i][2]));
    }
    cv::Mat frame = build_hsv_mask_bgra(portrait_frame_, gate_masks_, palette);
    cv::putText(frame, "HSV MASK", {10, 30}, cv::FONT_HERSHEY_SIMPLEX,
                0.7, cv::Scalar(255, 255, 255, 255), 2);
    for (int i = 0; i < 3; ++i) {
        if (!state_.gate_enabled[i]) continue;
        const int y = 52 + i * 28;
        cv::rectangle(frame, {10, y - 14}, {34, y + 8},
                      cv::Scalar(state_.gate_colors[i][2], state_.gate_colors[i][1],
                                 state_.gate_colors[i][0], 255), cv::FILLED);
        cv::putText(frame, "G" + std::to_string(i + 1), {42, y + 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255, 255), 1);
    }
    return frame;
}

void OpenCVApp::render_hsv() {
    hsv_frame_ = build_hsv_frame();
    cv::imshow(kHsvWindow, hsv_frame_);
}

void OpenCVApp::render_cam_settings() {
    cam_canvas_ = cv::Mat(420, 520, CV_8UC3, cv::Scalar(20, 20, 24));
    const auto ranges = CamSettingsConfig::ranges();
    int y = 30;
    for (std::size_t i = 0; i < ranges.size() && i < state_.cam_controls.size(); ++i, y += 42) {
        cv::putText(cam_canvas_, ranges[i].first, {20, y}, cv::FONT_HERSHEY_SIMPLEX,
                    0.5, cv::Scalar(210, 210, 210), 1);
        CvSlider slider;
        slider.rect = {230, y - 12, 260, 12};
        slider.min = ranges[i].second.first;
        slider.max = ranges[i].second.second;
        slider.value = static_cast<int>(state_.cam_controls[i]);
        slider.draw(cam_canvas_);
    }
    const auto buttons = cam_buttons();
    for (const auto& button : buttons) button.draw(cam_canvas_, false);
    cv::putText(cam_canvas_, cam_apply_status_text(state_.cam_apply_status), {12, 408},
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(160, 220, 160), 1);
    cv::imshow(kCamWindow, cam_canvas_);
    cv::setMouseCallback(kCamWindow, on_cam_mouse, this);
}

cv::Mat OpenCVApp::build_help_frame() const {
    cv::Mat frame(720, 900, CV_8UC3, cv::Scalar(16, 16, 20));
    const auto& sections = HelpRu::sections();
    const auto tabs = help_tabs();
    const int active = std::clamp(help_tab_, 0, static_cast<int>(tabs.size()) - 1);
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        tabs[i].draw(frame, i == active);
    }
    if (active < static_cast<int>(sections.size())) {
        int y = 62;
        for (const auto& line : sections[active].lines) {
            cv::putText(frame, line.substr(0, 110), {20, y}, cv::FONT_HERSHEY_SIMPLEX,
                        0.45, cv::Scalar(220, 220, 220), 1);
            y += 24;
        }
    }
    return frame;
}

void OpenCVApp::render_help() {
    help_canvas_ = build_help_frame();
    cv::imshow(kHelpWindow, help_canvas_);
    cv::setMouseCallback(kHelpWindow, on_help_mouse, this);
}

void OpenCVApp::handle_key(int key) {
    if (key == 27 || key == 'q' || key == 'Q') {
        quit_requested_ = true;
        return;
    }
    if (key == '\r' || key == '\n') {
        state_.input_active = false;
        need_connect_ = true;
        return;
    }
    if (state_.input_active) {
        if (key == 8) {
            backspace_source();
        } else if (key >= 32 && key < 127) {
            append_source_text(std::string_view(reinterpret_cast<const char*>(&key), 1));
        } else if (key == 22) {
            append_source_text(clipboard_utf8());
        }
        return;
    }
    switch (key) {
        case 'd': case 'D': state_.show_radar = !state_.show_radar; break;
        case 'h': case 'H': state_.show_hsv = !state_.show_hsv; break;
        case 'i': case 'I': state_.show_help = !state_.show_help; break;
        case 'c': case 'C': state_.show_cam_settings = !state_.show_cam_settings; break;
        case 'p': case 'P': state_.paused = !state_.paused; break;
        case 'r': case 'R': calibrate_lab(0); break;
        case '+': case '=': calibrate_lab(2); break;
        case '-': case '_': calibrate_lab(-2); break;
        case '1': state_.view_mode = AppState::ViewMode::Bitmap; break;
        case '2': state_.view_mode = AppState::ViewMode::LabL; break;
        case '3': state_.view_mode = AppState::ViewMode::LabA; break;
        case '4': state_.view_mode = AppState::ViewMode::LabB; break;
        default: break;
    }
}

void OpenCVApp::handle_mouse(int event, int x, int y, int flags) {
    (void)flags;
    if (event != cv::EVENT_LBUTTONDOWN && event != cv::EVENT_LBUTTONUP) return;

    if (input_rect().contains(x, y)) {
        state_.input_active = true;
        return;
    }
    if (connect_rect().contains(x, y)) {
        state_.input_active = false;
        need_connect_ = true;
        return;
    }

    const auto buttons = top_buttons();
    const int top = button_at(buttons, x, y);
    if (top >= 0 && event == cv::EVENT_LBUTTONDOWN) {
        switch (top) {
            case 0: state_.view_mode = AppState::ViewMode::Bitmap; break;
            case 1: state_.view_mode = AppState::ViewMode::LabL; break;
            case 2: state_.view_mode = AppState::ViewMode::LabA; break;
            case 3: state_.view_mode = AppState::ViewMode::LabB; break;
            case 4: state_.paused = !state_.paused; break;
            case 5: state_.show_cam_settings = !state_.show_cam_settings; break;
            case 6: state_.show_radar = !state_.show_radar; break;
            case 7: bind_field(); break;
            case 8: state_.position_preset = (state_.position_preset + 1) % 3; break;
            case 9: state_.show_hsv = !state_.show_hsv; break;
            case 10: state_.show_help = !state_.show_help; break;
            default: break;
        }
        return;
    }

    const auto sliders = bottom_sliders();
    for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
        if (!sliders[i].rect.contains(x, y) || event != cv::EVENT_LBUTTONDOWN) continue;
        const int value = sliders[i].value_from_x(x);
        const int slot = target_gate_;
        const auto record = gate_store_.gate(static_cast<GateId>(slot));
        GateLab lab = record && record->lab ? *record->lab : GateLab{50, 90, -20, 20, -20, 20};
        lab[i] = value;
        const auto roles = gate_store_.roles();
        gate_store_.upsert(static_cast<GateId>(slot),
                           {state_.gate_colors[slot - 1][0], state_.gate_colors[slot - 1][1],
                            state_.gate_colors[slot - 1][2]},
                           roles[slot - 1], lab);
        gate_store_.save_atomic();
        colors_dirty_ = true;
        state_.gate_save_status = "G" + std::to_string(slot) + " thr " + std::to_string(i) + "=" + std::to_string(value);
        log_.push(state_.gate_save_status);
        return;
    }

    const auto bottom = bottom_buttons();
    for (int i = 0; i < static_cast<int>(bottom.size()); ++i) {
        if (!bottom[i].rect.contains(x, y) || event != cv::EVENT_LBUTTONDOWN) continue;
        if (i == 0) {
            target_gate_ = target_gate_ % 3 + 1;
        } else {
            save_gate(i);
        }
        return;
    }

    const auto areas = preview_areas();
    if (areas[0].contains(x, y)) {
        const cv::Point local(x - areas[0].x, y - areas[0].y);
        if (event == cv::EVENT_LBUTTONDOWN) {
            selecting_ = true;
            selection_start_ = local;
            selection_current_ = local;
        } else {
            selection_current_ = local;
        }
        return;
    }
}

void OpenCVApp::send_cam_settings() {
    if (!publisher_.bound()) return;
    publisher_.publish_cam_settings(state_.cam_controls);
    state_.cam_apply_status = CamApplyStatus::Sending;
    last_cam_send_ = clock::now();
    log_.push("CAM SET sent to Pi");
}

void OpenCVApp::calibrate_lab(int delta) {
    const int slot = target_gate_;
    const std::size_t index = static_cast<std::size_t>(slot - 1);
    const auto record = gate_store_.gate(static_cast<GateId>(slot));
    GateLab lab = record && record->lab ? *record->lab : GateLab{50, 90, -20, 20, -20, 20};

    if (delta == 0) {
        // R: пересчёт диапазона по сохранённому цвету целевых ворот.
        const RgbLab measured = rgb_to_lab_u8(
            static_cast<uint8_t>(std::clamp(state_.gate_colors[index][0], 0, 255)),
            static_cast<uint8_t>(std::clamp(state_.gate_colors[index][1], 0, 255)),
            static_cast<uint8_t>(std::clamp(state_.gate_colors[index][2], 0, 255)));
        lab = {std::clamp(static_cast<int>(measured.L) - 10, 0, 100),
               std::clamp(static_cast<int>(measured.L) + 10, 0, 100),
               std::clamp(static_cast<int>(measured.A) - 15, -128, 127),
               std::clamp(static_cast<int>(measured.A) + 15, -128, 127),
               std::clamp(static_cast<int>(measured.B) - 15, -128, 127),
               std::clamp(static_cast<int>(measured.B) + 15, -128, 127)};
    } else {
        for (int i = 0; i < 6; ++i) {
            lab[i] = std::clamp(lab[i] + delta, i >= 2 ? -128 : 0, i >= 2 ? 127 : 100);
        }
    }

    const auto roles = gate_store_.roles();
    gate_store_.upsert(static_cast<GateId>(slot),
                       {state_.gate_colors[index][0], state_.gate_colors[index][1],
                        state_.gate_colors[index][2]},
                       roles[index], lab);
    gate_store_.save_atomic();
    colors_dirty_ = true;
    state_.gate_save_status = "G" + std::to_string(slot) + " LAB tuned";
    log_.push(state_.gate_save_status);
}

void OpenCVApp::bind_field() {
    std::vector<FieldObservationInput> observations;
    for (int i = 0; i < 3; ++i) {
        const GateTrackResult& result = track_results_[i];
        const RadarGate& radar = state_.radar_gates[i];
        observations.push_back({i + 1, state_.gate_enabled[i],
                                result.found || radar.found,
                                result.found ? result.angle_deg : radar.angle_deg,
                                result.dist_cm > 0.0F ? result.dist_cm : radar.distance_cm,
                                result.held});
    }
    const float offset = state_.position_preset == 1 ? 90.0F : 0.0F;
    const FieldBindResult result = field_map_.bind_from_results(observations, offset, 0.0F);
    state_.bind_field_status = result.ok ? "field bound"
        : (result.warning.empty() ? "bind failed" : result.warning);
    if (result.ok) field_map_.save_atomic();
    log_.push("BIND FIELD: " + state_.bind_field_status);
}

void OpenCVApp::set_gate_role(int slot, const std::string& role) {
    if (slot < 1 || slot > 3) return;
    const std::size_t index = static_cast<std::size_t>(slot - 1);
    const auto record = gate_store_.gate(static_cast<GateId>(slot));
    gate_store_.upsert(static_cast<GateId>(slot),
                       {state_.gate_colors[index][0], state_.gate_colors[index][1],
                        state_.gate_colors[index][2]},
                       role, record && record->lab ? record->lab : std::nullopt);
    gate_store_.save_atomic();
    colors_dirty_ = true;
    state_.gate_save_status = "G" + std::to_string(slot) + " role " + role;
    log_.push(state_.gate_save_status);
}

void OpenCVApp::handle_aux_mouse(const std::string& window, int event, int x, int y) {
    if (event != cv::EVENT_LBUTTONDOWN) return;

    if (window == "CAM SET") {
        const auto ranges = CamSettingsConfig::ranges();
        for (std::size_t i = 0; i < ranges.size() && i < state_.cam_controls.size(); ++i) {
            CvSlider slider;
            slider.rect = {230, 18 + static_cast<int>(i) * 42, 260, 12};
            slider.min = ranges[i].second.first;
            slider.max = ranges[i].second.second;
            if (slider.rect.contains(x, y)) {
                state_.cam_controls[i] = static_cast<float>(slider.value_from_x(x));
                return;
            }
        }
        switch (find_button(cam_buttons(), x, y)) {
            case 0:
                state_.cam_controls = {{300.0F, 0.0F, 0.0F, 100.0F, 60.0F, 32.0F, 1.0F, 0.0F}};
                state_.cam_apply_status = CamApplyStatus::Idle;
                log_.push("CAM SET defaults restored");
                break;
            case 1: {
                for (std::size_t i = 0; i < ranges.size() && i < state_.cam_controls.size(); ++i) {
                    cam_config_.set(ranges[i].first, state_.cam_controls[i]);
                }
                state_.cam_send_status = cam_config_.save() ? "saved" : "save failed";
                state_.cam_apply_status = CamApplyStatus::Applied;
                log_.push("CAM SET " + state_.cam_send_status);
                break;
            }
            case 2: send_cam_settings(); break;
            case 3: set_gate_role(target_gate_, "ours"); break;
            case 4: set_gate_role(target_gate_, "opponent"); break;
            default: break;
        }
        return;
    }

    if (window == "INFO") {
        const int pressed = find_button(help_tabs(), x, y);
        if (pressed >= 0) help_tab_ = pressed;
    }
}

int OpenCVApp::run() {
    load_saved_gates();
    cv::namedWindow(kMainWindow, cv::WINDOW_NORMAL);
    cv::resizeWindow(kMainWindow, kWindowW, kWindowH);
    cv::setMouseCallback(kMainWindow, on_mouse, this);
    publisher_.bind();

    while (!quit_requested_) {
        if (need_connect_) connect_source();

        cv::Mat frame;
        if (state_.source_kind == AppState::SourceKind::Pi) {
            PiVideoFrame video;
            if (video_client_.receive(video)) {
                frame = video.rgb.clone();
                if (video.has_status) {
                    status_json_ = video.status_json;
                    status_received_at_ = clock::now();
                    has_status_ = true;
                    status_ = parse_status_payload(status_json_, status_received_at_, status_received_at_);
                    apply_status(status_);
                }
            }
        } else if (capture_.isOpened()) {
            cv::Mat bgr;
            capture_ >> bgr;
            if (!bgr.empty()) cv::cvtColor(bgr, frame, cv::COLOR_BGR2RGB);
        }

        if (has_status_) {
            status_ = parse_status_payload(status_json_, status_received_at_, clock::now());
            apply_status(status_);
        }
        if (!frame.empty() && !state_.paused) process_frame(frame);
        publish_colors();
        if (state_.show_cam_settings &&
            clock::now() - last_cam_send_ >= std::chrono::seconds(5)) {
            send_cam_settings();
        }
        render_main();
        if (state_.show_radar) render_radar();
        else cv::destroyWindow(kRadarWindow);
        if (state_.show_hsv && !portrait_frame_.empty()) render_hsv();
        else cv::destroyWindow(kHsvWindow);
        if (state_.show_cam_settings) render_cam_settings();
        else cv::destroyWindow(kCamWindow);
        if (state_.show_help) render_help();
        else cv::destroyWindow(kHelpWindow);

        if (cv::getWindowProperty(kMainWindow, cv::WND_PROP_VISIBLE) < 1) {
            quit_requested_ = true;
            break;
        }
        const int key = cv::waitKey(1);
        if (key >= 0) handle_key(key);
    }

    video_client_.disconnect();
    capture_.release();
    publisher_.close();
    cv::destroyAllWindows();
    return 0;
}

} // namespace camera_cpp::laptop
