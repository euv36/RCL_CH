#include "widgets.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "imgui.h"

#include "cam_settings_config.hpp"
#include "clipboard.hpp"
#include "contracts.hpp"
#include "gate_store.hpp"
#include "help_ru.hpp"
#include "panels.hpp"
#include "paths.hpp"
#include "source_controller.hpp"
#include "theme.hpp"

#if defined(CAMERA_CPP_HAS_ZMQ) && CAMERA_CPP_HAS_ZMQ
#include <zmq.h>
#endif

namespace camera_cpp::laptop {
namespace {

ImVec4 to_imgui(const Rgba& color) {
    return ImVec4(color.r, color.g, color.b, color.a);
}

void sync_source_text(AppState& state) {
    state.camera_source_text = state.source_buffer;
}

const char* view_mode_name(AppState::ViewMode mode) {
    switch (mode) {
        case AppState::ViewMode::Bitmap: return "Bitmap";
        case AppState::ViewMode::LabL: return "L";
        case AppState::ViewMode::LabA: return "A";
        case AppState::ViewMode::LabB: return "B";
    }
    return "Bitmap";
}

void begin_region(const UiRect& rect, const char* id) {
    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    ImGui::BeginChild(id, ImVec2(rect.w, rect.h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// Отправляет JSON через PUB. Подтверждения нет: вызывающий обязан сообщать
// пользователю только факт отправки, не "применено на Pi".
bool send_json_pub(const std::string& endpoint, const std::string& payload) {
#if defined(CAMERA_CPP_HAS_ZMQ) && CAMERA_CPP_HAS_ZMQ
    void* context = zmq_ctx_new();
    if (context == nullptr) return false;
    void* socket = zmq_socket(context, ZMQ_PUB);
    if (socket == nullptr) {
        zmq_ctx_term(context);
        return false;
    }
    const int linger_ms = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    bool ok = zmq_connect(socket, endpoint.c_str()) == 0;
    if (ok) {
        // PUB теряет первые сообщения, пока подписка не установлена.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ok = zmq_send(socket, payload.data(), payload.size(), 0) == static_cast<int>(payload.size());
    }
    zmq_close(socket);
    zmq_ctx_term(context);
    return ok;
#else
    (void)endpoint;
    (void)payload;
    return false;
#endif
}

std::string pi_endpoint(const AppState& state, int port) {
    const std::string host = state.pi_host.empty() ? state.camera_source_text : state.pi_host;
    return "tcp://" + host + ":" + std::to_string(port);
}

} // namespace

void apply_dark_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.Colors[ImGuiCol_WindowBg] = to_imgui(PanelTheme::window_bg);
    style.Colors[ImGuiCol_Button] = to_imgui(PanelTheme::accent);
    style.Colors[ImGuiCol_ButtonHovered] = to_imgui(PanelTheme::accent_hovered);
    style.Colors[ImGuiCol_ButtonActive] = to_imgui(PanelTheme::accent_active);
    style.Colors[ImGuiCol_Text] = to_imgui(PanelTheme::text);
}

void paste_into_source(AppState& state) {
    const std::string text = paste_text_from_clipboard();
    if (text.empty()) {
        return;
    }
    std::snprintf(state.source_buffer, sizeof(state.source_buffer), "%s", text.c_str());
    sync_source_text(state);
}

void draw_source_panel(AppState& state) {
    ImGui::TextUnformatted("КАМЕРА / ИСТОЧНИК");
    ImGui::Separator();
    ImGui::TextUnformatted("local, индекс камеры или IP Pi");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##source", state.source_buffer, sizeof(state.source_buffer));
    state.input_active = ImGui::IsItemActive();
    if (ImGui::Button("ПОДКЛЮЧИТЬ")) {
        sync_source_text(state);
        apply_connect(state, parse_connect(state.camera_source_text));
    }
    if (!state.connect_error.empty()) {
        ImGui::TextColored(to_imgui(PanelTheme::warning), "%s", state.connect_error.c_str());
    }
    if (!state.pi_status_summary.empty()) {
        ImGui::TextUnformatted(state.pi_status_summary.c_str());
    }
    ImGui::Text("FPS UI: %.1f", state.fps_ui);
}

void draw_video_panels(AppState& state) {
    ImGui::TextUnformatted("ВИДЕО");
    ImGui::Separator();
    ImGui::Text("Режим: %s   Масштаб: %.1fx   %s", view_mode_name(state.view_mode),
                state.zoom, state.paused ? "ПАУЗА" : "ПОТОК");
    ImGui::TextUnformatted("Полный кадр 1920x1080 MJPG без обрезки поля зрения.");
    ImGui::TextUnformatted("Рабочий кадр Pi: 640x360; портрет ноутбука: 720x1280.");
}

void draw_hsv_mask(AppState& state) {
    ImGui::TextUnformatted("HSV MASK");
    ImGui::Separator();
    ImGui::TextUnformatted("Маски ворот совмещены 1:1 с портретным кадром.");
    ImGui::TextUnformatted("Границы маски рисуются точным цветом ворот.");
    if (state.show_hsv) {
        for (std::size_t i = 0; i < PanelTheme::gate_colors.size(); ++i) {
            ImGui::ColorButton(("##swatch" + std::to_string(i)).c_str(),
                               to_imgui(PanelTheme::gate_colors[i]),
                               ImGuiColorEditFlags_NoTooltip);
            ImGui::SameLine();
            ImGui::Text("G%zu", i + 1);
        }
    }
}

void draw_gates_panel(AppState& state) {
    ImGui::TextUnformatted("GATES");
    ImGui::Separator();
    for (std::size_t i = 0; i < state.gate_colors.size(); ++i) {
        const std::string id = std::to_string(i + 1);
        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox(("G" + id).c_str(), &state.gate_enabled[i]);
        ImGui::SameLine();
        float color[3]{
            static_cast<float>(state.gate_colors[i][0]) / 255.0F,
            static_cast<float>(state.gate_colors[i][1]) / 255.0F,
            static_cast<float>(state.gate_colors[i][2]) / 255.0F,
        };
        if (ImGui::ColorEdit3(("цвет##" + id).c_str(), color)) {
            for (int c = 0; c < 3; ++c) {
                state.gate_colors[i][c] = static_cast<int>(std::lround(color[c] * 255.0F));
            }
            state.radar_gates[i].rgb = state.gate_colors[i];
        }
        ImGui::SameLine();
        if (ImGui::Button(("SAVE G" + id).c_str())) {
            GateStore store;
            const std::string path = contract_path("saved_gates.json").string();
            store.load(path);
            const auto& rgb = state.gate_colors[i];
            store.upsert(static_cast<GateId>(i + 1),
                         GateColor{rgb[0], rgb[1], rgb[2]},
                         state.gate_enabled[i] ? "ours" : "unset");
            nlohmann::json message;
            message["gates"] = nlohmann::json::array();
            nlohmann::json gate = nlohmann::json::object();
            gate["id"] = static_cast<int>(i) + 1;
            gate["rgb"] = {rgb[0], rgb[1], rgb[2]};
            gate["role"] = state.gate_enabled[i] ? "ours" : "unset";
            message["gates"].push_back(std::move(gate));
            const bool saved = store.save_atomic();
            const bool sent = send_json_pub(pi_endpoint(state, ports::kColors), message.dump());
            state.gate_save_status = saved
                ? (sent ? "G" + id + ": сохранено и отправлено на Pi"
                        : "G" + id + ": сохранено локально, Pi недоступен")
                : "G" + id + ": не удалось сохранить конфигурацию";
        }
        ImGui::PopID();
    }
    if (!state.gate_save_status.empty()) {
        ImGui::TextUnformatted(state.gate_save_status.c_str());
    }
}

void draw_cam_settings(AppState& state) {
    ImGui::TextUnformatted("CAM SET");
    ImGui::Separator();
    const auto ranges = CamSettingsConfig::ranges();
    for (std::size_t i = 0; i < ranges.size() && i < state.cam_controls.size(); ++i) {
        ImGui::SliderFloat(ranges[i].first.c_str(), &state.cam_controls[i],
                           static_cast<float>(ranges[i].second.first),
                           static_cast<float>(ranges[i].second.second));
    }
    ImGui::TextUnformatted("auto_exposure не показан: Pi всегда принудительно manual.");
    if (ImGui::Button("ПРИМЕНИТЬ")) {
        CamSettingsConfig config;
        config.load(contract_path("camera_settings.json").string());
        nlohmann::json controls = nlohmann::json::object();
        for (std::size_t i = 0; i < ranges.size() && i < state.cam_controls.size(); ++i) {
            config.set(ranges[i].first, state.cam_controls[i]);
            controls[ranges[i].first] = static_cast<int>(state.cam_controls[i]);
        }
        const bool saved = config.save();
        state.cam_apply_status = saved ? CamApplyStatus::Applied : CamApplyStatus::Rejected;
        nlohmann::json message;
        message["cmd"] = "set";
        message["controls"] = std::move(controls);
        const bool sent = send_json_pub(pi_endpoint(state, ports::kCamSettings), message.dump());
        state.cam_send_status = sent
            ? "Отправлено на Pi (подтверждение недоступно)"
            : "Pi недоступен: настройки сохранены локально";
        if (!sent) {
            state.cam_apply_status = CamApplyStatus::Offline;
        }
    }
    ImGui::TextUnformatted(cam_apply_status_text(state.cam_apply_status));
    if (!state.cam_send_status.empty()) {
        ImGui::TextUnformatted(state.cam_send_status.c_str());
    }
}

void draw_radar(AppState& state) {
    ImGui::TextUnformatted("RADAR");
    ImGui::Separator();
    for (const RadarGate& gate : state.radar_gates) {
        if (!radar_gate_visible(gate)) {
            ImGui::Text("G%d: нет активного обнаружения", gate.id);
            continue;
        }
        ImGui::TextColored(to_imgui(PanelTheme::accent), "G%d: %.1f°  %.0f см",
                           gate.id, gate.angle_deg, gate.distance_cm);
    }
}

void draw_help(AppState& state) {
    if (state.show_help) {
        ImGui::OpenPopup("INFO");
        state.show_help = false;
    }
    if (ImGui::BeginPopupModal("INFO", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const HelpSection& section : HelpRu::sections()) {
            ImGui::TextColored(to_imgui(PanelTheme::accent), "%s", section.title.c_str());
            ImGui::Separator();
            for (const std::string& line : section.lines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        if (ImGui::Button("ЗАКРЫТЬ")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_shell(AppState& state) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const UiLayout layout = compute_ui_layout(display.x, display.y);

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("Camera", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);

    begin_region(layout.top_row, "##top_row");
    if (ImGui::Button("R")) {
        state.view_mode = static_cast<AppState::ViewMode>(
            (static_cast<int>(state.view_mode) + 1) % 4);
    }
    ImGui::SameLine();
    if (ImGui::Button("-")) state.zoom = std::max(0.5F, state.zoom - 0.1F);
    ImGui::SameLine();
    if (ImGui::Button("+")) state.zoom = std::min(4.0F, state.zoom + 0.1F);
    ImGui::SameLine();
    for (int mode = 0; mode < 4; ++mode) {
        if (ImGui::Button(view_mode_name(static_cast<AppState::ViewMode>(mode)))) {
            state.view_mode = static_cast<AppState::ViewMode>(mode);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(state.paused ? "RESUME" : "PAUSE")) state.paused = !state.paused;
    ImGui::SameLine();
    if (ImGui::Button("CAM SET")) state.show_cam_settings = !state.show_cam_settings;
    ImGui::SameLine();
    if (ImGui::Button("RADAR")) state.show_radar = !state.show_radar;
    ImGui::SameLine();
    if (ImGui::Button("BIND FIELD")) {
        state.bind_field_status = "Привязка к полю требует измеренных ворот (логика Task 9).";
    }
    ImGui::SameLine();
    if (ImGui::Button("POS")) {
        state.position_preset = (state.position_preset + 1) % 3;
    }
    ImGui::SameLine();
    if (ImGui::Button("HSV MASK")) state.show_hsv = !state.show_hsv;
    ImGui::SameLine();
    if (ImGui::Button("INFO")) state.show_help = true;
    ImGui::EndChild();

    begin_region(layout.left_video, "##video");
    draw_video_panels(state);
    ImGui::EndChild();

    begin_region(layout.left_mask, "##mask");
    draw_hsv_mask(state);
    ImGui::EndChild();

    begin_region(layout.right_source, "##source");
    draw_source_panel(state);
    if (state.show_cam_settings) {
        ImGui::Separator();
        draw_cam_settings(state);
    }
    ImGui::EndChild();

    begin_region(layout.gates_panel, "##gates");
    draw_gates_panel(state);
    if (state.show_radar) {
        ImGui::Separator();
        draw_radar(state);
    }
    ImGui::EndChild();

    begin_region(layout.bottom_controls, "##bottom");
    ImGui::Text("POS: %s", state.position_preset == 0 ? "CENTER" :
                state.position_preset == 1 ? "OWN GOAL" : "MANUAL");
    if (!state.bind_field_status.empty()) {
        ImGui::TextUnformatted(state.bind_field_status.c_str());
    }
    ImGui::EndChild();

    ImGui::End();
    draw_help(state);
}

} // namespace camera_cpp::laptop
