#include "widgets.hpp"

#include <cstdio>
#include <string>

#include "imgui.h"

#include "clipboard.hpp"
#include "source_controller.hpp"

namespace camera_cpp::laptop {
namespace {

constexpr ImVec4 kWindowBackground(0.07F, 0.08F, 0.10F, 1.0F);
constexpr ImVec4 kAccent(0.35F, 0.65F, 0.95F, 1.0F);

void sync_source_text(AppState& state) {
    state.camera_source_text = state.source_buffer;
}

} // namespace

void apply_dark_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.Colors[ImGuiCol_WindowBg] = kWindowBackground;
    style.Colors[ImGuiCol_Button] = kAccent;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45F, 0.74F, 1.0F, 1.0F);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25F, 0.52F, 0.82F, 1.0F);
}

void paste_into_source(AppState& state) {
    const std::string text = paste_text_from_clipboard();
    if (text.empty()) {
        return;
    }
    std::snprintf(state.source_buffer, sizeof(state.source_buffer), "%s", text.c_str());
    sync_source_text(state);
}

void draw_shell(AppState& state) {
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("CAMERA / NATIVE");
    ImGui::Separator();
    ImGui::TextUnformatted("Источник камеры");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360.0F);
    ImGui::InputText("##source", state.source_buffer, sizeof(state.source_buffer));
    state.input_active = ImGui::IsItemActive();
    ImGui::SameLine();
    if (ImGui::Button("ПОДКЛЮЧИТЬ")) {
        sync_source_text(state);
        const ConnectRequest request = parse_connect(state.camera_source_text);
        apply_connect(state, request);
    }

    if (!state.connect_error.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "%s", state.connect_error.c_str());
    }
    if (!state.pi_status_summary.empty()) {
        ImGui::TextUnformatted(state.pi_status_summary.c_str());
    }
    ImGui::Text("FPS UI: %.1f", state.fps_ui);
    ImGui::End();
}

} // namespace camera_cpp::laptop
