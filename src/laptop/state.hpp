#pragma once

#include <array>
#include <string>

#include "cam_settings_panel.hpp"
#include "radar_panel.hpp"

namespace camera_cpp::laptop {

struct AppState {
    enum class SourceKind { Local, Pi, None };
    enum class ViewMode { Bitmap, LabL, LabA, LabB };

    AppState();

    std::string camera_source_text = "10.132.122.51";
    char source_buffer[512]{};
    bool input_active = false;
    SourceKind source_kind = SourceKind::None;
    std::string pi_status_summary;
    double fps_ui = 0;

    std::string pi_host;
    int pi_port = 5555;
    int local_index = -1;
    std::string connect_error;

    // View controls (top row).
    ViewMode view_mode = ViewMode::Bitmap;
    float zoom = 1.0F;
    bool paused = false;

    // Panel visibility toggles.
    bool show_help = false;
    bool show_radar = false;
    bool show_hsv = false;
    bool show_cam_settings = false;

    // Gate editors and radar inputs. Local saved configuration is authoritative
    // for `gate_enabled`; Pi status only supplies `found` / measurements.
    std::array<bool, 3> gate_enabled{{false, false, false}};
    std::array<std::array<int, 3>, 3> gate_colors{{
        {{255, 255, 0}},
        {{0, 128, 255}},
        {{255, 128, 0}},
    }};
    std::array<RadarGate, 3> radar_gates;
    std::string gate_save_status;

    // Field binding / position preset.
    std::string bind_field_status;
    int position_preset = 0; // 0 CENTER, 1 OWN GOAL, 2 MANUAL

    // Camera settings panel.
    CamApplyStatus cam_apply_status = CamApplyStatus::Idle;
    std::string cam_send_status;
    std::array<float, 8> cam_controls{{300.0F, 0.0F, 0.0F, 100.0F, 60.0F, 32.0F, 1.0F, 0.0F}};
};

} // namespace camera_cpp::laptop
